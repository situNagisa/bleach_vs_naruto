/// 并发度测试：图的峰值并发是否达到理论最大值，同时不违反依赖。
///
/// 测法：每个节点体进入时给"正在运行"计数加一并更新峰值，离开时减一。需要验证并发的层，节点体在一个
/// 会合点上互相等待，直到人数到齐才离开——于是"能同时跑"就一定会被测到；如果图或调度器把它们串行化了，
/// 会合会超时（判失败），不会卡死。每个节点进入时还检查前一层是否已经全部结束，保证并发不是靠违反依赖得来的。
///
///   1. 1 -> 8 -> 1，内联            理论峰值 1（全部在调用线程上同步跑完）
///   2. 1 -> 8 -> 1，线程池          理论峰值 8
///   3. 1 -> 4 -> 8 -> 1，线程池     理论峰值 8（相邻两层全连接，4 与 8 不会重叠）
///   4. (1 -> 4 -> 8 -> 1) + (x -> y -> z)：两张图作为节点组装进一张外层图，线程池
///                                   理论峰值 8 + 1 = 9（8 宽的那层与链上的一个节点同时跑）
///
/// 线程池用 bwos_params{.blockSize = 1}。exec::static_thread_pool 默认 blockSize = 8：worker 线程自己提交的任务
/// 先进本地块，块写满才发布给别的线程窃取，所以"在 worker 上扇出"的任务会全部滞留在这一个 worker 上。
/// 这是调度器的窃取粒度，与图无关——第 0 项用纯 stdexec（不经过 sdag）对照演示这一点。

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <print>
#include <thread>
#include <tuple>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <sdag/sdag.h>

#include "check.h"

namespace
{

using namespace ::std::chrono_literals;

// ---------------------------------------------------------------- 观测工具

struct tracker
{
	::std::atomic<int> running{0};
	::std::atomic<int> peak{0};
	::std::atomic<int> violations{0};

	void enter() noexcept
	{
		int const now = running.fetch_add(1, ::std::memory_order_acq_rel) + 1;
		int seen = peak.load(::std::memory_order_relaxed);
		while (now > seen && !peak.compare_exchange_weak(seen, now, ::std::memory_order_relaxed))
		{
		}
	}

	void leave() noexcept
	{
		running.fetch_sub(1, ::std::memory_order_acq_rel);
	}
};

/// 到齐 target 个才放行；等太久就记为超时并放行（测试判失败，但不会卡死）。
struct rendezvous
{
	int target;
	::std::chrono::milliseconds patience;
	::std::atomic<int> arrived{0};
	::std::atomic<bool> timed_out{false};

	explicit rendezvous(int count, ::std::chrono::milliseconds wait = 10s) noexcept : target(count), patience(wait)
	{
	}

	void arrive_and_wait() noexcept
	{
		arrived.fetch_add(1, ::std::memory_order_acq_rel);
		auto const deadline = ::std::chrono::steady_clock::now() + patience;
		while (arrived.load(::std::memory_order_acquire) < target)
		{
			if (::std::chrono::steady_clock::now() >= deadline)
			{
				timed_out.store(true, ::std::memory_order_relaxed);
				return;
			}
			::std::this_thread::sleep_for(20us);
		}
	}
};

// ---------------------------------------------------------------- 分层图：相邻两层全连接

template<::std::size_t... Sizes>
struct layers
{
	static constexpr ::std::array<::std::size_t, sizeof...(Sizes)> sizes{Sizes...};
	static constexpr ::std::size_t depth = sizeof...(Sizes);
	static constexpr ::std::size_t total = (Sizes + ...);

	static constexpr auto layer_of = []
	{
		::std::array<::std::size_t, total> result{};
		::std::size_t node = 0;
		for (::std::size_t layer = 0; layer < depth; ++layer)
		{
			for (::std::size_t index = 0; index < sizes[layer]; ++index)
			{
				result[node++] = layer;
			}
		}
		return result;
	}();

	static constexpr auto index_of = []
	{
		::std::array<::std::size_t, total> result{};
		::std::size_t node = 0;
		for (::std::size_t layer = 0; layer < depth; ++layer)
		{
			for (::std::size_t index = 0; index < sizes[layer]; ++index)
			{
				result[node++] = index;
			}
		}
		return result;
	}();
};

template<class Graph, ::std::size_t Layer, ::std::size_t Index>
struct key
{
};

template<class Graph, ::std::size_t Layer, class Indices>
struct previous_layer;

template<class Graph, ::std::size_t Layer, ::std::size_t... J>
struct previous_layer<Graph, Layer, ::std::index_sequence<J...>>
{
	using type = ::sdag::data<key<Graph, Layer - 1, J>...>;
};

/// 第 Layer 层依赖第 Layer - 1 层的全部节点。
template<class Graph, class Layers, ::std::size_t Layer>
[[nodiscard]] consteval auto dependencies_of() noexcept
{
	if constexpr (Layer == 0)
	{
		return ::std::type_identity<::sdag::data<>>{};
	}
	else
	{
		return ::std::type_identity<typename previous_layer<Graph, Layer, ::std::make_index_sequence<Layers::sizes[Layer - 1]>>::type>{};
	}
}

template<class Graph, class Layers, ::std::size_t Layer>
using dependencies_t = typename decltype(dependencies_of<Graph, Layers, Layer>())::type;

/// 一张分层图运行时的观测状态：每层结束了几个节点、每层用哪个会合点（空指针表示不等）。
template<class Layers>
struct layered_state
{
	tracker* probe;
	::std::array<rendezvous*, Layers::depth> meet{};
	::std::array<::std::atomic<int>, Layers::depth> done{};
	/// 这张图作为外层图的节点时，它的根要等外层的前驱结束（空指针表示没有外层前驱）。
	::std::atomic<bool> const* gate = nullptr;
};

/// 节点体：根返回 1，其余返回输入之和，所以汇点的值能核对数据流。
template<class Layers, ::std::size_t Layer>
struct node_body
{
	layered_state<Layers>* state;

	template<class... Inputs>
	int operator()(Inputs const&... inputs) const noexcept
	{
		state->probe->enter();
		if constexpr (Layer == 0)
		{
			if (state->gate && !state->gate->load(::std::memory_order_acquire))
			{
				state->probe->violations.fetch_add(1, ::std::memory_order_relaxed);
			}
		}
		else
		{
			if (state->done[Layer - 1].load(::std::memory_order_acquire) != static_cast<int>(Layers::sizes[Layer - 1]))
			{
				state->probe->violations.fetch_add(1, ::std::memory_order_relaxed);
			}
		}
		if (rendezvous* meet = state->meet[Layer])
		{
			meet->arrive_and_wait();
		}
		state->done[Layer].fetch_add(1, ::std::memory_order_release);
		state->probe->leave();
		if constexpr (sizeof...(Inputs) == 0)
		{
			return 1;
		}
		else
		{
			return (inputs + ...);
		}
	}
};

/// 内联：根是 just() | then，其余是 then。
template<class Layers>
struct inline_payloads
{
	layered_state<Layers>* state;

	template<::std::size_t Layer>
	[[nodiscard]] auto make() const
	{
		if constexpr (Layer == 0)
		{
			return ::stdexec::just() | ::stdexec::then(node_body<Layers, Layer>{state});
		}
		else
		{
			return ::stdexec::then(node_body<Layers, Layer>{state});
		}
	}
};

/// 线程池：根是 schedule | then，其余是 continues_on | then。
template<class Layers, class Scheduler>
struct pool_payloads
{
	layered_state<Layers>* state;
	Scheduler scheduler;

	template<::std::size_t Layer>
	[[nodiscard]] auto make() const
	{
		if constexpr (Layer == 0)
		{
			return ::stdexec::schedule(scheduler) | ::stdexec::then(node_body<Layers, Layer>{state});
		}
		else
		{
			return ::stdexec::continues_on(scheduler) | ::stdexec::then(node_body<Layers, Layer>{state});
		}
	}
};

template<class Graph, class Layers, class Payloads>
[[nodiscard]] auto make_layered(Payloads const& payloads)
{
	return [&]<::std::size_t... N>(::std::index_sequence<N...>)
	{
		return ::sdag::dag(
			::sdag::make_node<key<Graph, Layers::layer_of[N], Layers::index_of[N]>, dependencies_t<Graph, Layers, Layers::layer_of[N]>>(
				payloads.template make<Layers::layer_of[N]>())...);
	}(::std::make_index_sequence<Layers::total>{});
}

/// 汇点的期望值：第 0 层每个节点是 1，之后每层每个节点是上一层之和。
template<class Layers>
[[nodiscard]] constexpr int expected_sink() noexcept
{
	int value = 1;
	for (::std::size_t layer = 1; layer < Layers::depth; ++layer)
	{
		value *= static_cast<int>(Layers::sizes[layer - 1]);
	}
	return value;
}

struct graph_a;
struct graph_b;
struct start_key;
struct a_key;
struct b_key;
struct join_key;

/// 扇出后继要能被别的 worker 立刻窃取，见文件开头的说明。
[[nodiscard]] ::exec::bwos_params stealable() noexcept
{
	return {.numBlocks = 32, .blockSize = 1};
}

void report(char const* name, tracker const& probe, int expected_peak)
{
	::std::println("  {}：峰值 {}，理论 {}", name, probe.peak.load(), expected_peak);
}

// ---------------------------------------------------------------- 0. 调度器对照（纯 stdexec）

/// 在 worker 线程上用 when_all 一次提交 8 个任务，测能同时跑几个。
[[nodiscard]] int stdexec_fan_peak(::exec::static_thread_pool& pool, ::std::chrono::milliseconds patience)
{
	tracker probe;
	rendezvous meet{8, patience};
	auto scheduler = pool.get_scheduler();
	auto body = [&]
	{
		probe.enter();
		meet.arrive_and_wait();
		probe.leave();
	};
	auto one = [&] { return ::stdexec::schedule(scheduler) | ::stdexec::then(body); };
	auto fan = ::stdexec::schedule(scheduler) | ::stdexec::let_value([&]
	{
		return ::stdexec::when_all(one(), one(), one(), one(), one(), one(), one(), one());
	});
	::stdexec::sync_wait(::std::move(fan));
	return probe.peak.load();
}

void test_scheduler_baseline()
{
	test::section("对照：纯 stdexec 在 worker 上扇出 8 个任务（不经过 sdag）");
	::exec::static_thread_pool stealable_pool{16, stealable()};
	int const stealable_peak = stdexec_fan_peak(stealable_pool, 10s);
	::std::println("  blockSize = 1：峰值 {}", stealable_peak);
	test::check(stealable_peak == 8, "blockSize = 1 时 8 个任务同时运行");
	::exec::static_thread_pool default_pool{16};
	::std::println("  默认 blockSize = 8：峰值 {}（任务滞留在提交它们的 worker 上，会合点超时后才放行）", stdexec_fan_peak(default_pool, 100ms));
}

// ---------------------------------------------------------------- 1. 内联

void test_inline()
{
	test::section("1 -> 8 -> 1，内联：全部在调用线程上同步完成，峰值 1");
	using shape = layers<1, 8, 1>;
	tracker probe;
	layered_state<shape> state{&probe};
	auto graph = make_layered<graph_a, shape>(inline_payloads<shape>{&state});
	auto [value] = ::stdexec::sync_wait(::std::move(graph)).value();
	report("sdag", probe, 1);
	test::check(value == expected_sink<shape>(), "汇点收到 8 路输入之和");
	test::check(probe.peak.load() == 1, "峰值 1");
	test::check(probe.violations.load() == 0, "没有节点在前驱结束之前启动");
}

// ---------------------------------------------------------------- 2、3. 线程池上的分层图

template<class Shape>
void run_layered_on_pool(char const* name, ::exec::static_thread_pool& pool, int expected_peak)
{
	tracker probe;
	layered_state<Shape> state{&probe};
	::std::array<::std::optional<rendezvous>, Shape::depth> meets{};
	for (::std::size_t layer = 0; layer < Shape::depth; ++layer)
	{
		if (Shape::sizes[layer] > 1)
		{
			meets[layer].emplace(static_cast<int>(Shape::sizes[layer]));
			state.meet[layer] = &*meets[layer];
		}
	}
	auto graph = make_layered<graph_a, Shape>(pool_payloads<Shape, decltype(pool.get_scheduler())>{&state, pool.get_scheduler()});
	auto [value] = ::stdexec::sync_wait(::std::move(graph)).value();
	report(name, probe, expected_peak);
	bool timed_out = false;
	for (auto const& meet : meets)
	{
		timed_out = timed_out || (meet && meet->timed_out.load());
	}
	test::check(!timed_out, "每一层都在会合点到齐（没有被串行化）");
	test::check(probe.peak.load() == expected_peak, "峰值达到理论最大");
	test::check(value == expected_sink<Shape>(), "汇点的值正确");
	test::check(probe.violations.load() == 0, "没有节点在前驱结束之前启动");
}

void test_pool_fan()
{
	test::section("1 -> 8 -> 1，线程池：峰值 8");
	::exec::static_thread_pool pool{16, stealable()};
	run_layered_on_pool<layers<1, 8, 1>>("sdag", pool, 8);
}

void test_pool_two_layers()
{
	test::section("1 -> 4 -> 8 -> 1，线程池：峰值 8");
	::exec::static_thread_pool pool{16, stealable()};
	run_layered_on_pool<layers<1, 4, 8, 1>>("sdag", pool, 8);
}

// ---------------------------------------------------------------- 4. 图组装成图

void test_nested()
{
	test::section("(1 -> 4 -> 8 -> 1) + (x -> y -> z)，两张图作为节点组装成一张图，线程池：峰值 9");
	using shape_a = layers<1, 4, 8, 1>;
	using shape_b = layers<1, 1, 1>;
	::exec::static_thread_pool pool{16, stealable()};
	auto scheduler = pool.get_scheduler();

	tracker probe;
	rendezvous meet_a1{4};
	// A 的 8 宽那层与 B 的 y 在同一个会合点：只有两张图真的并行，9 个人才能到齐。
	rendezvous meet_wide{9};
	layered_state<shape_a> state_a{&probe};
	state_a.meet[1] = &meet_a1;
	state_a.meet[2] = &meet_wide;
	layered_state<shape_b> state_b{&probe};
	state_b.meet[1] = &meet_wide;
	// 外层图的依赖也插桩：两张内层图的根要在 start 之后，join 要在两张内层图的汇点之后。
	::std::atomic<bool> started{false};
	state_a.gate = &started;
	state_b.gate = &started;

	auto a = make_layered<graph_a, shape_a>(pool_payloads<shape_a, decltype(scheduler)>{&state_a, scheduler});
	auto b = make_layered<graph_b, shape_b>(pool_payloads<shape_b, decltype(scheduler)>{&state_b, scheduler});
	static_assert(::exec::sender_for<decltype(a), ::sdag::dag_t>, "内层图本身就是一个图 sender");

	auto outer = ::sdag::dag(
		// 先等一会儿再置位：内层图的根若被提前启动，就一定会看到 started 还是假。
		::sdag::make_node<start_key>(::stdexec::schedule(scheduler) | ::stdexec::then([&]
		{
			::std::this_thread::sleep_for(5ms);
			started.store(true, ::std::memory_order_release);
		})),
		::sdag::make_node<a_key, ::sdag::control<start_key>>(::std::move(a)),
		::sdag::make_node<b_key, ::sdag::control<start_key>>(::std::move(b)),
		::sdag::make_node<join_key, ::sdag::data<a_key, b_key>>(::stdexec::then([&](int from_a, int from_b)
		{
			if (state_a.done[shape_a::depth - 1].load() != 1 || state_b.done[shape_b::depth - 1].load() != 1)
			{
				probe.violations.fetch_add(1, ::std::memory_order_relaxed);
			}
			return from_a + from_b;
		})));

	auto [value] = ::stdexec::sync_wait(::std::move(outer)).value();
	report("sdag", probe, 9);
	test::check(!meet_a1.timed_out.load() && !meet_wide.timed_out.load(), "两张内层图真的并行（会合点到齐）");
	test::check(probe.peak.load() == 9, "峰值达到理论最大 8 + 1");
	test::check(value == expected_sink<shape_a>() + expected_sink<shape_b>(), "外层汇点收到两张内层图的值");
	test::check(probe.violations.load() == 0, "没有节点在前驱结束之前启动（含外层图的依赖）");
}

}

int main()
{
	test_scheduler_baseline();
	test_inline();
	test_pool_fan();
	test_pool_two_layers();
	test_nested();
	return test::report();
}
