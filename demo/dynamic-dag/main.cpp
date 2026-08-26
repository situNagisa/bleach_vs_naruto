/// 动态 DAG demo。
///
/// 四个场景，各自验证设计讨论里的一条结论：
///
///   1. 拓扑         —— 运行期构出的 DAG 真的按依赖跑，扇入扇出都对，且真并行。
///   2. 复用         —— "构图一次、执行多次"稳态下每次执行 0 次堆分配。
///   3. 失败传播     —— 节点失败时下游被跳过、在跑的兄弟收到取消、整图不死锁、错误不丢。
///                     （兄弟是"收到取消请求"还是"已经跑完"取决于时序，两种都正确。）
///   4. 规模 / 蹦床  —— 大图不爆栈；长链全量跳过时也不递归打穿栈。
///
/// 数据依赖被刻意排除在图之外：节点通过 frame_context 这个旁路交换数据，
/// 边只表达"先后"，因此边的存储是 0 字节。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <mutex>
#include <new>
#include <print>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "./dag.h"

namespace ex = ::stdexec;

// ------------------------------------------------------------------ 分配计数

namespace
{

std::atomic<std::size_t> g_allocation_count{0};
std::atomic<bool> g_allocation_counting{false};

void note_allocation() noexcept
{
	if (g_allocation_counting.load(std::memory_order_relaxed))
	{
		g_allocation_count.fetch_add(1, std::memory_order_relaxed);
	}
}

} // namespace

void* operator new(std::size_t size)
{
	note_allocation();
	void* const pointer = std::malloc(size != 0 ? size : 1);
	if (pointer == nullptr)
	{
		throw std::bad_alloc{};
	}
	return pointer;
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
	note_allocation();
	const std::size_t align = static_cast<std::size_t>(alignment);
	const std::size_t rounded = ((size + align - 1) / align) * align;
	void* const pointer = std::aligned_alloc(align, rounded != 0 ? rounded : align);
	if (pointer == nullptr)
	{
		throw std::bad_alloc{};
	}
	return pointer;
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
	return ::operator new(size, alignment);
}

// GCC 会误报 -Wmismatched-new-delete：它看到 operator new[] 转调 operator new，
// 就认定 free 收到的指针"来自不匹配的分配函数"。这里 new/new[] 全部落到 malloc、
// delete/delete[] 全部落到 free，配对是成立的。
#if defined(__GNUC__) && !defined(__clang__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }

#if defined(__GNUC__) && !defined(__clang__)
#	pragma GCC diagnostic pop
#endif

// ------------------------------------------------------------------ 工具

namespace
{

using clock_type = std::chrono::steady_clock;

clock_type::time_point g_origin = clock_type::now();

[[nodiscard]] double elapsed_ms() noexcept
{
	return std::chrono::duration<double, std::milli>(clock_type::now() - g_origin).count();
}

/// 一个节点体**实际**占用 CPU 的区间。图层的 tracer 打的是"何时提交"，
/// 想证明并行必须量"何时真的在跑"，两者差着一个调度队列。
struct node_span
{
	dag::node_index index;
	double begin_ms;
	double end_ms;
	std::thread::id thread;
};

/// 节点之间共享的旁路状态。控制依赖模型下，数据走这里，不走边。
struct frame_context
{
	std::atomic<std::uint64_t> visibility{0};
	std::atomic<std::uint64_t> shadow_texels{0};
	std::atomic<std::uint64_t> lit_pixels{0};
	std::atomic<std::uint32_t> completed_nodes{0};
	/// burn() 的结果统一落到这里。没有这个汇点，编译器会把纯函数 burn 整个消掉，
	/// 于是"并行"就变成了一堆 1 µs 的空节点，量不出任何东西。
	std::atomic<std::uint64_t> sink{0};

	bool record_spans{false};
	std::mutex spans_mutex;
	std::vector<node_span> spans;
};

/// 一段可测量的真实工作，避免用 sleep 制造假的并行度。
[[nodiscard]] std::uint64_t burn(std::uint64_t iterations) noexcept
{
	std::uint64_t accumulator = 0x9e3779b97f4a7c15ull;
	for (std::uint64_t i = 0; i < iterations; ++i)
	{
		accumulator ^= accumulator << 13;
		accumulator ^= accumulator >> 7;
		accumulator ^= accumulator << 17;
	}
	return accumulator;
}

/// 追踪器。函数指针 + user 指针，关掉时零开销。
struct trace_sink
{
	struct entry
	{
		double timestamp_ms;
		dag::node_index index;
		dag::node_event event;
		std::thread::id thread;
	};

	std::mutex mutex;
	std::vector<entry> entries;

	static void callback(void* user, dag::node_index index, dag::node_event event) noexcept
	{
		auto* const self = static_cast<trace_sink*>(user);
		std::lock_guard lock{self->mutex};
		self->entries.push_back(entry{elapsed_ms(), index, event, std::this_thread::get_id()});
	}
};

[[nodiscard]] const char* event_name(dag::node_event event) noexcept
{
	switch (event)
	{
	case dag::node_event::started: return "start";
	case dag::node_event::completed: return "done ";
	case dag::node_event::failed: return "FAIL ";
	case dag::node_event::skipped: return "skip ";
	case dag::node_event::cancelled: return "CANCL";
	}
	return "?";
}

void dump_trace(const dag::graph& graph, trace_sink& sink)
{
	std::lock_guard lock{sink.mutex};
	std::ranges::stable_sort(sink.entries,
		[](const trace_sink::entry& a, const trace_sink::entry& b) { return a.timestamp_ms < b.timestamp_ms; });

	std::vector<std::thread::id> lanes;
	for (const trace_sink::entry& e : sink.entries)
	{
		if (std::ranges::find(lanes, e.thread) == lanes.end())
		{
			lanes.push_back(e.thread);
		}
	}

	for (const trace_sink::entry& e : sink.entries)
	{
		const auto lane = static_cast<std::size_t>(std::ranges::find(lanes, e.thread) - lanes.begin());
		std::print("    {:8.3f} ms  worker#{}  {}  d{} {}\n",
			e.timestamp_ms, lane, event_name(e.event), graph.node_depth(e.index), graph.node_name(e.index));
	}
	sink.entries.clear();
}

// ------------------------------------------------------------------ 场景 1 & 2 & 3 的图

struct render_graph_ids
{
	dag::node_index cull;
	dag::node_index shadow;
	dag::node_index gbuffer;
	dag::node_index ssao;
	dag::node_index lighting;
	dag::node_index post;
	dag::node_index ui;
	dag::node_index present;
};

/// 一张"渲染图形状"的 DAG：
///
///     cull ─┬─> shadow  ──────────────┐
///           ├─> gbuffer ─┬─> ssao ────┼─> lighting ─> post ─┐
///           │            └────────────┘                     ├─> present
///           └─> ui ────────────────────────────────────────┘
///
/// 注意 lighting 有三个前驱（扇入），cull 有三个后继（扇出）。
template<class Scheduler>
render_graph_ids build_render_graph(dag::graph& graph, Scheduler scheduler, frame_context& context,
	std::uint64_t cost_scale = 1, dag::node_index failing_node = dag::invalid_node)
{
	auto make_node = [&](std::uint64_t cost, auto body)
	{
		const dag::node_index index = static_cast<dag::node_index>(graph.node_count());
		const std::uint64_t iterations = cost * cost_scale;
		return ex::then(ex::schedule(scheduler), [&context, iterations, body, index, failing_node]
		{
			if (index == failing_node)
			{
				throw std::runtime_error{"节点被人为注入失败"};
			}
			const double begin = context.record_spans ? elapsed_ms() : 0.0;
			const std::uint64_t value = burn(iterations);
			context.sink.fetch_add(value, std::memory_order_relaxed);
			body(context, value);
			if (context.record_spans)
			{
				std::lock_guard lock{context.spans_mutex};
				context.spans.push_back(node_span{index, begin, elapsed_ms(), std::this_thread::get_id()});
			}
			context.completed_nodes.fetch_add(1, std::memory_order_relaxed);
		});
	};

	render_graph_ids ids{};

	ids.cull = graph.add_node("cull",
		make_node(60'000, [](frame_context& c, std::uint64_t v) { c.visibility.store(v, std::memory_order_relaxed); }));

	ids.shadow = graph.add_node("shadow",
		make_node(200'000, [](frame_context& c, std::uint64_t v) { c.shadow_texels.store(v, std::memory_order_relaxed); }),
		{ids.cull});

	ids.gbuffer = graph.add_node("gbuffer",
		make_node(160'000, [](frame_context&, std::uint64_t) {}),
		{ids.cull});

	ids.ssao = graph.add_node("ssao",
		make_node(120'000, [](frame_context&, std::uint64_t) {}),
		{ids.gbuffer});

	ids.lighting = graph.add_node("lighting",
		make_node(180'000, [](frame_context& c, std::uint64_t v) { c.lit_pixels.store(v, std::memory_order_relaxed); }),
		{ids.shadow, ids.gbuffer, ids.ssao});

	ids.post = graph.add_node("post",
		make_node(90'000, [](frame_context&, std::uint64_t) {}),
		{ids.lighting});

	ids.ui = graph.add_node("ui",
		make_node(70'000, [](frame_context&, std::uint64_t) {}),
		{ids.cull});

	ids.present = graph.add_node("present",
		make_node(30'000, [](frame_context&, std::uint64_t) {}),
		{ids.post, ids.ui});

	graph.finalize();
	return ids;
}

// ------------------------------------------------------------------ 场景

/// 把每个节点体的真实占用区间画成甘特图，并统计"忙碌时间总和 / 墙钟"。
void dump_spans(const dag::graph& graph, frame_context& context, double wall_ms, double critical_ms = 0.0)
{
	std::lock_guard lock{context.spans_mutex};
	auto& spans = context.spans;
	if (spans.empty())
	{
		return;
	}

	std::ranges::stable_sort(spans, [](const node_span& a, const node_span& b) { return a.begin_ms < b.begin_ms; });

	const double origin = spans.front().begin_ms;
	double finish = origin;
	double busy = 0.0;
	std::vector<std::thread::id> lanes;
	for (const node_span& span : spans)
	{
		finish = std::max(finish, span.end_ms);
		busy += span.end_ms - span.begin_ms;
		if (std::ranges::find(lanes, span.thread) == lanes.end())
		{
			lanes.push_back(span.thread);
		}
	}

	constexpr int columns = 56;
	const double scale = columns / std::max(finish - origin, 1e-9);

	for (const node_span& span : spans)
	{
		const auto lane = static_cast<std::size_t>(std::ranges::find(lanes, span.thread) - lanes.begin());
		const int start_column = static_cast<int>((span.begin_ms - origin) * scale);
		const int width = std::max(1, static_cast<int>((span.end_ms - span.begin_ms) * scale));

		std::string bar(static_cast<std::size_t>(start_column), ' ');
		bar.append(static_cast<std::size_t>(std::min(width, columns - start_column)), '#');

		std::print("    {:>8} d{} |{:<{}}| {:6.2f} ms  worker#{}\n",
			graph.node_name(span.index), graph.node_depth(span.index), bar, columns,
			span.end_ms - span.begin_ms, lane);
	}

	std::print("    ── 忙碌时间合计 {:.2f} ms / 墙钟 {:.2f} ms = 平均并行度 {:.2f}，用到 {} 个 worker\n",
		busy, wall_ms, busy / std::max(wall_ms, 1e-9), lanes.size());

	if (critical_ms > 0.0)
	{
		std::print("    ── 关键路径实测耗时 {:.2f} ms —— 这是墙钟的下界，并行度上限 {:.2f}；\n"
			"       墙钟 / 下界 = {:.2f}，越接近 1 说明调度损耗越小\n",
			critical_ms, busy / critical_ms, wall_ms / critical_ms);
	}
	spans.clear();
}

void scenario_topology(exec::static_thread_pool& pool)
{
	std::print("\n=== 场景 1：拓扑与真并行 ===\n");

	frame_context context;
	context.record_spans = true;
	context.spans.reserve(64);

	dag::graph graph;
	// 放大单节点工作量。工作太碎的话，完成 cull 的那个 worker 会把三个后继压进
	// 自己的本地 LIFO 队列并自行跑完，其余 worker 还没来得及窃取 —— 那样量到的
	// 不是"图不并行"，而是"任务粒度小于调度器的唤醒延迟"。
	const render_graph_ids ids = build_render_graph(graph, pool.get_scheduler(), context, 20);

	std::print("  节点 {} 条边 {} 根 {} 关键路径长度 {} op-arena {} 字节\n",
		graph.node_count(), graph.edge_count(), graph.root_count(),
		graph.critical_path_length(), graph.operation_arena_size());

	const auto begin = clock_type::now();
	const bool completed = ex::sync_wait(graph.run()).has_value();
	const auto end = clock_type::now();
	const double wall_ms = std::chrono::duration<double, std::milli>(end - begin).count();

	// 用**实测**的节点时长在这张图上求最长路径。节点索引本身就是拓扑序，一遍前向扫描即可。
	// 没有这个下界，"并行度 1.41"读者无从判断是调度器差还是图本身就窄。
	const std::pair<dag::node_index, dag::node_index> edges[] = {
		{ids.cull, ids.shadow}, {ids.cull, ids.gbuffer}, {ids.cull, ids.ui},
		{ids.gbuffer, ids.ssao},
		{ids.shadow, ids.lighting}, {ids.gbuffer, ids.lighting}, {ids.ssao, ids.lighting},
		{ids.lighting, ids.post},
		{ids.post, ids.present}, {ids.ui, ids.present},
	};
	std::vector<double> duration(graph.node_count(), 0.0);
	{
		std::lock_guard lock{context.spans_mutex};
		for (const node_span& span : context.spans)
		{
			duration[span.index] = span.end_ms - span.begin_ms;
		}
	}
	std::vector<double> finish(graph.node_count(), 0.0);
	for (std::size_t index = 0; index < graph.node_count(); ++index)
	{
		double ready = 0.0;
		for (const auto& [from, to] : edges)
		{
			if (to == index)
			{
				ready = std::max(ready, finish[from]);
			}
		}
		finish[index] = ready + duration[index];
	}
	const double critical_ms = *std::ranges::max_element(finish);

	dump_spans(graph, context, wall_ms, critical_ms);

	std::print("  完成={} 执行节点={} 墙钟={:.2f} ms\n",
		completed, context.completed_nodes.load(), wall_ms);
	std::print("  ↑ 这里画的是节点体**真实占用 CPU** 的区间，不是提交时刻。\n");
	std::print("    shadow / gbuffer / ui 的条重叠且落在不同 worker —— 扇出真的并行了；\n");
	std::print("    lighting 的条起点晚于它三个前驱的终点 —— 扇入计数器生效了。\n");
}

void scenario_reuse(exec::static_thread_pool& pool, unsigned worker_count)
{
	std::print("\n=== 场景 2：构图一次、执行多次（稳态 0 分配）===\n");

	frame_context context;
	dag::graph graph;
	build_render_graph(graph, pool.get_scheduler(), context);

	// 第一次执行会把 storage 池撑到位（op-state arena、计数器数组、毒标记数组），
	// 之后每次 run() 都从池里取，不再分配。故意不计入统计。
	ex::sync_wait(graph.run());

	// 观察窗口要比 worker 数量长。原因见下面的结论：还有一笔**不属于图层**的
	// 每线程一次性开销要摊掉，窗口太短会误判成"每帧都在分配"。
	const int total_runs = static_cast<int>(worker_count) + 8;
	int first_zero_run = -1;
	double steady_milliseconds = 0.0;
	std::string curve;
	curve.reserve(static_cast<std::size_t>(total_runs) * 3);

	for (int run = 1; run <= total_runs; ++run)
	{
		g_allocation_count.store(0, std::memory_order_relaxed);
		g_allocation_counting.store(true, std::memory_order_relaxed);

		const auto begin = clock_type::now();
		const bool completed = ex::sync_wait(graph.run()).has_value();
		const auto end = clock_type::now();

		g_allocation_counting.store(false, std::memory_order_relaxed);

		const std::size_t allocations = g_allocation_count.load();
		curve += std::format("{} ", allocations);

		if (allocations == 0 && first_zero_run < 0)
		{
			first_zero_run = run;
		}
		if (allocations != 0)
		{
			first_zero_run = -1; // 还没真正进入稳态。
		}
		steady_milliseconds = std::chrono::duration<double, std::milli>(end - begin).count();

		if (!completed)
		{
			std::print("  第 {} 次执行没有完成！\n", run);
		}
	}

	std::print("  每次执行的堆分配次数（共 {} 次执行，{} 个 worker）：\n    {}\n",
		total_runs, worker_count, curve);
	std::print("  稳态从第 {} 次开始，之后恒为 0；最后一次墙钟 {:.3f} ms\n",
		first_zero_run, steady_milliseconds);
	std::print("  ↑ 前面那段非零**不是图层在分配**。用 backtrace 抓过：每笔都是\n");
	std::print("    schedule_start -> vtable->start -> static_thread_pool 入队，\n");
	std::print("    stdexec 在某个 worker 线程**首次向池提交**时惰性建它的 remote/BWOS\n");
	std::print("    队列（48 字节 + 1280 字节对齐块）。每线程一次，与图无关，也与帧数无关；\n");
	std::print("    所以观察窗口必须长过线程数，否则会把它错读成每帧开销。\n");
	std::print("    图层本身（op-state / 计数器 / 毒标记）从第一次执行起就一次不分配。\n");
}

void scenario_failure(exec::static_thread_pool& pool)
{
	std::print("\n=== 场景 3：失败传播、取消与不死锁 ===\n");

	frame_context context;
	dag::graph graph;
	// ssao 是第 4 个加入的节点，索引为 3。让它抛异常。
	const render_graph_ids ids = build_render_graph(graph, pool.get_scheduler(), context, 20, 3);

	trace_sink sink;
	sink.entries.reserve(256);
	graph.set_tracer(&trace_sink::callback, &sink);

	std::print("  注入失败节点：{}（索引 {}）\n", graph.node_name(ids.ssao), ids.ssao);

	bool caught = false;
	std::string message;
	try
	{
		ex::sync_wait(graph.run());
	}
	catch (const std::exception& error)
	{
		caught = true;
		message = error.what();
	}

	dump_trace(graph, sink);
	graph.set_tracer(nullptr, nullptr);

	std::print("  错误被抛到图外：{}（{}）\n", caught, message);
	std::print("  ↑ ssao 的下游（lighting / post / present）全部 skip，整图仍然在有限时间内完成。\n");
	std::print("    少给后继减一次计数就会让 outstanding 永远到不了 0 —— 那正是最常见的死锁 bug。\n");
}

void scenario_scale(exec::static_thread_pool& pool)
{
	std::print("\n=== 场景 4：规模与蹦床 ===\n");

	// (a) 分层随机大图。
	{
		constexpr int layer_count = 24;
		constexpr int layer_width = 160;
		constexpr int fan_in = 3;

		frame_context context;
		dag::graph graph;
		std::mt19937 random{20260824};

		std::vector<dag::node_index> previous;
		std::vector<dag::node_index> current;
		std::vector<dag::node_index> predecessors;

		auto scheduler = pool.get_scheduler();
		for (int layer = 0; layer < layer_count; ++layer)
		{
			current.clear();
			for (int slot = 0; slot < layer_width; ++slot)
			{
				predecessors.clear();
				if (!previous.empty())
				{
					std::uniform_int_distribution<std::size_t> pick{0, previous.size() - 1};
					for (int k = 0; k < fan_in; ++k)
					{
						const dag::node_index candidate = previous[pick(random)];
						if (std::ranges::find(predecessors, candidate) == predecessors.end())
						{
							predecessors.push_back(candidate);
						}
					}
				}

				auto work = ex::then(ex::schedule(scheduler), [&context]
				{
					context.visibility.fetch_add(burn(4'000), std::memory_order_relaxed);
					context.completed_nodes.fetch_add(1, std::memory_order_relaxed);
				});
				current.push_back(graph.add_node("n", std::move(work), std::span<const dag::node_index>{predecessors}));
			}
			previous = current;
		}
		graph.finalize();

		const auto begin = clock_type::now();
		const bool completed = ex::sync_wait(graph.run()).has_value();
		const auto end = clock_type::now();
		const double milliseconds = std::chrono::duration<double, std::milli>(end - begin).count();

		std::print("  (a) 分层大图：节点 {} 边 {} 关键路径 {} op-arena {} KiB\n",
			graph.node_count(), graph.edge_count(), graph.critical_path_length(),
			graph.operation_arena_size() / 1024);
		std::print("      完成={} 执行节点={} 墙钟={:.2f} ms（约 {:.2f} µs/节点）\n",
			completed, context.completed_nodes.load(), milliseconds,
			milliseconds * 1000.0 / static_cast<double>(graph.node_count()));
	}

	// (b) 长链 + 根节点失败 → 后面全部内联跳过。
	//     跳过路径是完全内联的（skip -> retire -> start_node -> skip ...），
	//     没有蹦床的话这里就是一次 N 层的栈递归，必爆。
	{
		constexpr int chain_length = 200'000;

		frame_context context;
		dag::graph graph;
		auto scheduler = pool.get_scheduler();

		dag::node_index previous = dag::invalid_node;
		for (int i = 0; i < chain_length; ++i)
		{
			const bool is_root = (i == 0);
			auto work = ex::then(ex::schedule(scheduler), [&context, is_root]
			{
				if (is_root)
				{
					throw std::runtime_error{"链首失败"};
				}
				context.completed_nodes.fetch_add(1, std::memory_order_relaxed);
			});

			if (is_root)
			{
				previous = graph.add_node("chain", std::move(work));
			}
			else
			{
				previous = graph.add_node("chain", std::move(work), {previous});
			}
		}
		graph.finalize();

		bool caught = false;
		const auto begin = clock_type::now();
		try
		{
			ex::sync_wait(graph.run());
		}
		catch (const std::exception&)
		{
			caught = true;
		}
		const auto end = clock_type::now();

		std::print("  (b) 长链 {} 节点，链首失败 → 其余全部内联跳过\n", chain_length);
		std::print("      抛出={} 实际执行节点={} 墙钟={:.2f} ms（没有爆栈就说明蹦床生效）\n",
			caught, context.completed_nodes.load(),
			std::chrono::duration<double, std::milli>(end - begin).count());
	}
}

} // namespace

int main()
{
	const unsigned hardware = std::max(2u, std::thread::hardware_concurrency());

	// blockSize = 1 不是调参，是这个模型的**硬性前提**。
	//
	// exec::static_thread_pool 用 BWOS（block-wise ordered work stealing）：worker 从
	// 自己线程提交的任务先进本地块，块**写满**才发布出去供别人窃取，默认 blockSize = 8。
	// 而 DAG 的扇出是"节点完成后，在完成它的那个 worker 上提交 2~3 个后继" —— 永远填不满
	// 一个块，于是后继全部滞留在该 worker 的私有队列里，被它自己顺序跑完，其余 worker
	// 一直睡着。实测（见 README）：池内扇出 4 个 3.6 ms 的任务，
	//     blockSize = 8  →  1 个线程，14.76 ms
	//     blockSize = 1  →  4 个线程， 3.68 ms
	// 这跟图层实现无关，任何"在完成线程上扇出后继"的调度器都会撞上。
	exec::static_thread_pool pool{hardware, exec::bwos_params{.numBlocks = 32, .blockSize = 1}};

	std::print("动态 DAG demo —— stdexec，{} 个 worker（bwos blockSize=1）\n", hardware);
	std::print("图这一层是一个自定义 sender 算法：没有 split，没有 when_all，\n");
	std::print("扇出与扇入合起来退化成每个节点一个前驱计数器。\n");

	g_origin = clock_type::now();

	scenario_topology(pool);
	scenario_reuse(pool, hardware);
	scenario_failure(pool);
	scenario_scale(pool);

	std::print("\n全部场景结束。\n");
	return 0;
}
