/// consumer-arch 的帧流程，改写在 job-arch 的 entity / task 两层结构上。
///
/// 跟同目录的 `demo.cpp` 是**同一个场景**：一个 renderer 负责开帧 / 提交 / 等 GPU，
/// 若干 draw entity 往里挂录制。区别只在架构：
///
/// | | `demo.cpp`（旧） | 这里（新） |
/// | --- | --- | --- |
/// | 注册 | `entity::begin_frame(fc)` 主动 `add_job` | `entity_storage::add`，容器的事 |
/// | 构建 | 两阶段：先全部 `add_job`，再 `build_jobs()` | 一阶段：`build_all`，依赖方按需把被依赖方拽起来 |
/// | 查别人 | `frame_context::job<T>()`，能顺手 `add_job` | `entity_view::entity<T>()`，**只能查** |
/// | 缺席 | `job<T>()` 返回 nullopt，抛异常 | 同样返回 nullopt，调用方自己降级 |
/// | 等待 | nagisa 协程 `consumer_task` | `::stdexec::sync_wait` |
///
/// **Vulkan / SDL / vkfu / nagisa 全部换成下面那个 `mock_gpu`**，所以这个 TU 只依赖
/// stdexec + EnTT，能在没有图形栈的机器上直接跑。被保留的是架构上真正有意义的两条性质：
/// 帧槽数量有限（必须等 GPU 跑完才能复用），以及帧槽必须归还（出错 / 取消也不能漏）。

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <exec/split.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "../job-arch/dynamic_when_all.h"
#include "../job-arch/entities.h"
#include "../job-arch/node_sender.h"


// ------------------------------------------------------------------ 事件日志

::std::mutex g_log_mutex;
::std::vector<::std::string> g_log;

auto log_event(::std::string text) -> void
{
	auto const guard = ::std::lock_guard{g_log_mutex};
	g_log.push_back(::std::move(text));
}

auto take_log() -> ::std::vector<::std::string>
{
	auto const guard = ::std::lock_guard{g_log_mutex};
	return ::std::exchange(g_log, {});
}

auto join_log() -> ::std::string
{
	auto joined = ::std::string{};
	for (auto&& entry : take_log())
	{
		if (!joined.empty())
		{
			joined += " ";
		}
		joined += entry;
	}
	return joined;
}

// -------------------------------------------------------------------- 假 GPU

/// 替掉 Vulkan 的那一层。只保留两条架构上有意义的性质：
///   1. 帧槽数量有限——拿不到就得等上一帧的 GPU 跑完（背压）；
///   2. 帧槽必须归还——正常、出错、被取消三条路都不能漏。
///
/// 槽位只有一个，于是"上一帧没归还"会直接把下一帧卡住；`acquire` 带超时，
/// 把死锁变成一条看得懂的错误。
struct mock_gpu
{
	struct slot
	{
		::std::size_t _index = 0;
		::std::uint64_t _frame = 0;
		::std::vector<::std::string> _commands;
	};

	::std::mutex _mutex;
	::std::condition_variable _released;
	::std::vector<slot> _slots;
	::std::vector<bool> _busy;

	explicit mock_gpu(::std::size_t count)
		: _slots(count)
		, _busy(count, false)
	{
		for (auto index = ::std::size_t{0}; index != count; ++index)
		{
			_slots[index]._index = index;
		}
	}

	/// 拿一个空闲帧槽；没有就等 GPU 归还。
	[[nodiscard]] auto acquire(::std::uint64_t frame_index) -> slot*
	{
		auto lock = ::std::unique_lock{_mutex};
		auto const ready = _released.wait_for(lock, ::std::chrono::seconds{2}, [this]
		{
			return ::std::ranges::find(_busy, false) != _busy.end();
		});
		if (!ready)
		{
			throw ::std::runtime_error{"mock gpu: 等不到空闲帧槽——上一帧的槽位泄漏了"};
		}

		auto const found = static_cast<::std::size_t>(
			::std::ranges::find(_busy, false) - _busy.begin());
		_busy[found] = true;

		auto&& target = _slots[found];
		target._frame = frame_index;
		target._commands.clear();
		log_event("acquire#" + ::std::to_string(found));
		return &target;
	}

	auto release(slot* target) noexcept -> void
	{
		{
			auto const guard = ::std::lock_guard{_mutex};
			_busy[target->_index] = false;
		}
		_released.notify_one();
	}

	[[nodiscard]] auto all_free() -> bool
	{
		auto const guard = ::std::lock_guard{_mutex};
		return ::std::ranges::find(_busy, true) == _busy.end();
	}
};

/// 帧槽的 RAII 持有者。**析构即归还**——所以出错 / 取消导致下游不跑时，
/// 槽位照样跟着 op-state 一起释放，不会漏。
struct frame_target
{
	mock_gpu* _gpu;
	mock_gpu::slot* _slot;

	frame_target(mock_gpu& gpu, mock_gpu::slot& target) noexcept
		: _gpu(&gpu)
		, _slot(&target)
	{
	}

	frame_target(frame_target&&) = delete;
	auto operator=(frame_target&&) -> frame_target& = delete;

	~frame_target() { _gpu->release(_slot); }
};

/// 开帧节点产出的值。共享是必须的：所有录制者都要拿到同一个帧槽。
using frame_handle = ::std::shared_ptr<frame_target>;

// -------------------------------------------------------------- 使用者的帧上下文

struct frame
{
	::exec::static_thread_pool::scheduler _scheduler;
	mock_gpu* _gpu = nullptr;
	::std::vector<node_sender> _roots;
	::stdexec::inplace_stop_token _stop_token;
	::std::uint64_t _index = 0;
	// 跟 job-arch 一样：没有任何 entity 相关的成员。查找能力由 `build_task` 的参数送进来。

	frame(::exec::static_thread_pool::scheduler scheduler,
		mock_gpu& gpu,
		::std::uint64_t index,
		::stdexec::inplace_stop_token stop_token = {})
		: _scheduler(scheduler)
		, _gpu(&gpu)
		, _stop_token(stop_token)
		, _index(index)
	{
	}
};

// ------------------------------------------------------------------ renderer

/// 开帧节点：申请帧槽 + 开始录制。所有录制者共享，所以过 `split`。
template <class Functor>
[[nodiscard]] auto make_begin_node(frame& context, Functor functor)
{
	return ::stdexec::starts_on(context._scheduler, ::stdexec::just())
		| ::stdexec::then(::std::move(functor))
		// 干活的节点自己把取消令牌写进环境。结构边一律不传取消——`::exec::split` 在订阅者
		// 令牌已停止时直接 `set_stopped`、根本不启动共享体，取消要是沿结构边下压，
		// 收尾节点就一次都不跑。
		| ::stdexec::write_env(::stdexec::prop{::stdexec::get_stop_token, context._stop_token})
		| ::exec::split();
}

/// 汇合节点：开帧完了排干录制名单、提交、等 GPU。
template <class OpeningSender, class DrainFunctor, class WaitFunctor>
[[nodiscard]] auto make_fence_node(OpeningSender opening, DrainFunctor drain, WaitFunctor wait)
{
	return ::std::move(opening)
		| ::stdexec::let_value(::std::move(drain))
		| ::stdexec::then(::std::move(wait));
}

struct renderer
{
	mock_gpu* _gpu = nullptr;

	explicit renderer(mock_gpu& gpu) noexcept
		: _gpu(&gpu)
	{
	}

	struct open_frame
	{
		renderer* _self;
		::std::uint64_t _frame_index;

		auto operator()() const -> frame_handle
		{
			auto* const target = _self->_gpu->acquire(_frame_index);
			log_event("begin#" + ::std::to_string(_frame_index));
			return ::std::make_shared<frame_target>(*_self->_gpu, *target);
		}
	};

	struct begin
	{
		decltype(make_begin_node(::std::declval<frame&>(), ::std::declval<open_frame>())) _sender;

		begin(renderer& self, frame& context)
			: _sender(make_begin_node(context, open_frame{&self, context._index}))
		{
		}

		begin(begin&&) = delete;
		auto operator=(begin&&) -> begin& = delete;
	};

	/// 提交：把录制下来的命令交给"GPU"。
	struct submit_frame
	{
		frame_handle _target;

		auto operator()() const -> frame_handle
		{
			log_event("submit#" + ::std::to_string(_target->_slot->_frame)
				+ "[" + ::std::to_string(_target->_slot->_commands.size()) + "]");
			return _target;
		}
	};

	/// 等 GPU 跑完。返回之后那个 `frame_handle` 就没人引用了，帧槽随之归还。
	struct wait_frame
	{
		auto operator()(frame_handle const& target) const -> void
		{
			log_event("wait#" + ::std::to_string(target->_slot->_frame));
		}
	};

	/// 汇合点的尾巴。写成具名别名，`drain_recorders` 才能写出显式返回类型——
	/// 它嵌在 `renderer` 里，推导返回类型在 `fence` 的成员声明处还用不了。
	using recorder_tail = decltype(
		dynamic_when_all(::std::declval<::std::vector<node_sender>>())
		| ::stdexec::continues_on(::std::declval<::exec::static_thread_pool::scheduler>())
		| ::stdexec::then(::std::declval<submit_frame>()));

	/// 排干录制名单。**在启动期才跑**，所以"谁先构建"不影响谁进得来。
	struct drain_recorders
	{
		::std::vector<node_sender>* _recorders;
		bool* _sealed;
		::exec::static_thread_pool::scheduler _scheduler;

		auto operator()(frame_handle const& target) const -> recorder_tail
		{
			*_sealed = true;
			return dynamic_when_all(::std::move(*_recorders))
				| ::stdexec::continues_on(_scheduler)
				| ::stdexec::then(submit_frame{target});
		}
	};

	struct fence
	{
		::std::vector<node_sender> _recorders;
		bool _sealed = false;
		decltype(make_fence_node(
			::std::declval<decltype(begin::_sender)>(),
			::std::declval<drain_recorders>(),
			::std::declval<wait_frame>())) _sender;

		fence(begin& opening, frame& context)
			: _sender(make_fence_node(
				opening._sender,
				drain_recorders{&_recorders, &_sealed, context._scheduler},
				wait_frame{}))
		{
		}

		// 地址敏感：`_sender` 里攥着 `_recorders` / `_sealed` 的地址。
		fence(fence&&) = delete;
		auto operator=(fence&&) -> fence& = delete;

		/// @pre 汇合点尚未封存（本帧还没开始跑）。
		auto add_recorder(node_sender node) -> void
		{
			assert(!_sealed && "consumer-arch: 录制名单已封存，注册来晚了");
			_recorders.push_back(::std::move(node));
		}
	};

	auto build_task(frame& context, entity_view<frame>, task_builder builder) -> void
	{
		auto&& opening = builder.emplace<begin>(*this, context);
		auto&& joining = builder.emplace<fence>(opening, context);
		context._roots.push_back(make_node(joining._sender));
	}
};

// ---------------------------------------------------------------- draw entity

/// 录制节点：等开帧拿到帧槽，切到 pool 上录，最后把值丢掉（`node_sender` 不传值）。
template <class OpeningSender, class Functor>
[[nodiscard]] auto make_record_node(
	OpeningSender opening, ::exec::static_thread_pool::scheduler scheduler, Functor functor)
{
	return ::std::move(opening)
		| ::stdexec::continues_on(scheduler)
		| ::stdexec::then(::std::move(functor))
		| ::stdexec::then([] {});
}

/// 一个类型至多一个 entity，所以多个录制者靠**类型**区分——`demo.cpp` 里的
/// `template <::std::size_t EntityId> struct entity` 是同一套办法。
template <::std::size_t PainterId>
struct painter
{
	static constexpr auto id = PainterId;

	::std::string _name;
	bool _fail = false;

	struct do_record
	{
		painter* _self;

		auto operator()(frame_handle const& target) const -> void
		{
			if (_self->_fail)
			{
				throw ::std::runtime_error{"painter " + _self->_name + ": 录制失败"};
			}
			target->_slot->_commands.push_back(_self->_name);
			log_event("record:" + _self->_name + "#" + ::std::to_string(target->_slot->_frame));
		}
	};

	struct record
	{
		decltype(make_record_node(
			::std::declval<decltype(renderer::begin::_sender)>(),
			::std::declval<::exec::static_thread_pool::scheduler>(),
			::std::declval<do_record>())) _sender;

		record(painter& self, renderer::begin& opening, frame& context)
			: _sender(make_record_node(opening._sender, context._scheduler, do_record{&self}))
		{
		}

		record(record&&) = delete;
		auto operator=(record&&) -> record& = delete;
	};

	auto build_task(frame& context, entity_view<frame> entities, task_builder builder) -> void
	{
		// renderer 本帧不参与就降级：什么都不录。旧版这里是抛异常。
		auto renderer_entity = entities.entity<renderer>();
		if (!renderer_entity)
		{
			log_event("skip:" + _name);
			return;
		}

		// 隐式构建：直接取 task，renderer 没构建就顺手把它构建了。
		auto* const opening = renderer_entity->task<renderer::begin>();
		assert(opening != nullptr && "consumer-arch: renderer 没有登记 begin");

		auto&& recording = builder.emplace<record>(*this, *opening, context);

		auto* const joining = renderer_entity->task<renderer::fence>();
		assert(joining != nullptr && "consumer-arch: renderer 没有登记 fence");
		joining->add_recorder(make_node(::std::move(recording._sender)));
	}
};

// ------------------------------------------------------------------- 跑一帧

/// 直接用 `::stdexec::sync_wait`：它的接收者环境不提供 `get_stop_token`，
/// 根节点拿到的是 `never_stop_token`——正好符合"结构边不传取消"。
auto run_frame(frame& context) -> bool
{
	return ::stdexec::sync_wait(dynamic_when_all(::std::move(context._roots))).has_value();
}

// -------------------------------------------------------------------- 场景

int g_failures = 0;

auto check(char const* title, bool ok) -> void
{
	::std::printf("  %-36s %s\n", title, ok ? "ok" : "FAIL");
	if (!ok)
	{
		++g_failures;
	}
}

auto report(char const* title, ::std::string const& expected, ::std::string const& actual) -> void
{
	auto const ok = actual == expected;
	::std::printf("  %-36s %s\n", title, ok ? "ok" : "FAIL");
	if (!ok)
	{
		::std::printf("      期望: %s\n      实际: %s\n", expected.c_str(), actual.c_str());
		++g_failures;
	}
}

/// 录制是并发的，顺序不定；把中间那段排序后再比。
auto normalize(::std::vector<::std::string> entries) -> ::std::string
{
	auto const first = ::std::ranges::find_if(entries,
		[](auto&& entry) { return entry.starts_with("record:"); });
	auto const last = ::std::ranges::find_if_not(first, entries.end(),
		[](auto&& entry) { return entry.starts_with("record:"); });
	::std::ranges::sort(first, last);

	auto joined = ::std::string{};
	for (auto&& entry : entries)
	{
		if (!joined.empty())
		{
			joined += " ";
		}
		joined += entry;
	}
	return joined;
}

int main()
{
	auto pool = ::exec::static_thread_pool{4};
	auto const scheduler = pool.get_scheduler();

	// 只有一个帧槽：上一帧不归还，下一帧就卡死（`acquire` 两秒超时会抛出来）。
	auto gpu = mock_gpu{1};

	auto draw = renderer{gpu};
	auto red = painter<0>{._name = "red"};
	auto blue = painter<1>{._name = "blue"};

	::std::printf("consumer-arch on job-arch（Vulkan 已 mock）\n");

	// 1. 一帧：开帧 → 两个录制 → 提交 → 等 GPU
	//
	// 帧槽的归还时机值得单独看一眼：开帧节点是 `split` 的，产出的 `frame_handle` 被
	// **共享状态**攥着，而共享状态活在 `begin` 这个 task 里。所以帧槽不是 `run_frame`
	// 一返回就还，而是本帧的 task 池（`entity_storage`）销毁时才还。旧版 `demo.cpp`
	// 也是这个时序——那边共享状态挂在 `frame_context::_jobs` 上。
	{
		{
			auto context = frame{scheduler, gpu, 1};
			auto world = entity_storage<frame>{};
			world.add(draw);
			world.add(red);
			world.add(blue);
			build_all(world, context);
			take_log();

			auto const finished = run_frame(context);
			check("1 单帧 · 跑完没被取消", finished);
			report("1 单帧 · 执行顺序",
				"acquire#0 begin#1 record:blue#1 record:red#1 submit#1[2] wait#1",
				normalize(take_log()));
			check("1 单帧 · task 池还在时帧槽仍被占", !gpu.all_free());
		}
		check("1 单帧 · task 池销毁后帧槽归还", gpu.all_free());
	}

	// 2. 注册顺序反过来，结果一致（painter 先构建，把 renderer 拽起来）
	{
		auto context = frame{scheduler, gpu, 2};
		auto world = entity_storage<frame>{};
		world.add(red);
		world.add(blue);
		world.add(draw);
		build_all(world, context);
		take_log();

		run_frame(context);
		report("2 注册顺序无关",
			"acquire#0 begin#2 record:blue#2 record:red#2 submit#2[2] wait#2",
			normalize(take_log()));
	}

	// 3. 连跑三帧：只有一个帧槽，跑得通就说明每帧都归还了
	{
		auto ok = true;
		for (auto index = ::std::uint64_t{3}; index != 6; ++index)
		{
			auto context = frame{scheduler, gpu, index};
			auto world = entity_storage<frame>{};
			world.add(draw);
			world.add(red);
			world.add(blue);
			build_all(world, context);
			take_log();
			ok = run_frame(context) && ok;
			auto const seen = normalize(take_log());
			auto const expected = "acquire#0 begin#" + ::std::to_string(index)
				+ " record:blue#" + ::std::to_string(index)
				+ " record:red#" + ::std::to_string(index)
				+ " submit#" + ::std::to_string(index) + "[2]"
				+ " wait#" + ::std::to_string(index);
			ok = (seen == expected) && ok;
		}
		check("3 连跑三帧 · 单槽复用", ok && gpu.all_free());
	}

	// 4. renderer 本帧不参与：painter 降级，不录也不崩（旧版这里是抛异常）
	{
		auto context = frame{scheduler, gpu, 6};
		auto world = entity_storage<frame>{};
		world.add(red);
		world.add(blue);
		build_all(world, context);

		auto const seen = normalize(take_log());
		auto const finished = run_frame(context);
		check("4 renderer 缺席 · 不崩", finished);
		report("4 renderer 缺席 · 降级", "skip:red skip:blue", seen);
		check("4 renderer 缺席 · 没占帧槽", gpu.all_free());
	}

	// 5. 录制出错：错误传上来，**帧槽照样归还**
	{
		red._fail = true;
		auto caught = ::std::string{"(没抛)"};
		{
			auto context = frame{scheduler, gpu, 7};
			auto world = entity_storage<frame>{};
			world.add(draw);
			world.add(red);
			world.add(blue);
			build_all(world, context);
			take_log();

			try
			{
				run_frame(context);
			}
			catch (::std::exception const& error)
			{
				caught = error.what();
			}
			take_log();
		}
		check("5 录制出错 · 错误传上来", caught == "painter red: 录制失败");
		check("5 录制出错 · 帧槽不泄漏", gpu.all_free());
		red._fail = false;
	}

	// 6. 整帧取消：干活的节点全跳过，帧槽也没被占住
	{
		auto source = ::stdexec::inplace_stop_source{};
		source.request_stop();

		auto context = frame{scheduler, gpu, 8, source.get_token()};
		auto world = entity_storage<frame>{};
		world.add(draw);
		world.add(red);
		world.add(blue);
		build_all(world, context);
		take_log();

		auto const finished = run_frame(context);
		auto const seen = join_log();
		check("6 整帧取消", !finished && seen.empty());
	}
	check("6 整帧取消 · 帧槽不泄漏", gpu.all_free());

	// 7. 出错之后还能继续跑下一帧（帧槽真的回来了）
	{
		auto context = frame{scheduler, gpu, 9};
		auto world = entity_storage<frame>{};
		world.add(draw);
		world.add(red);
		world.add(blue);
		build_all(world, context);
		take_log();

		run_frame(context);
		report("7 出错之后仍能开新帧",
			"acquire#0 begin#9 record:blue#9 record:red#9 submit#9[2] wait#9",
			normalize(take_log()));
	}

	::std::printf("%s\n", g_failures == 0 ? "全部通过" : "有失败");
	return g_failures == 0 ? 0 : 1;
}
