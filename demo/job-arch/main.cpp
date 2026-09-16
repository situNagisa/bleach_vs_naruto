/// job-arch 演示：entity / task 两层 + 需求驱动的构建顺序。
///
/// 库那边（`entities.h`）只管 entity 和 task。根节点容器、停止令牌、scheduler、帧号
/// 全在下面这个 `frame` 里——它是**使用者的**类型，库只按模板参数把它转发给
/// `build_task`。`entities.h` 一行 sender 都不认识。

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <typeindex>

#include <exec/split.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "./dynamic_when_all.h"
#include "./entities.h"
#include "./node_sender.h"


// ------------------------------------------------------------------ 事件日志

::std::mutex g_log_mutex;
::std::vector<::std::string> g_log;

auto log_event(::std::string text) -> void
{
	auto const guard = ::std::lock_guard{g_log_mutex};
	g_log.push_back(::std::move(text));
}

auto take_log() -> ::std::string
{
	auto const guard = ::std::lock_guard{g_log_mutex};
	auto joined = ::std::string{};
	for (auto&& entry : g_log)
	{
		if (!joined.empty())
		{
			joined += " ";
		}
		joined += entry;
	}
	g_log.clear();
	return joined;
}

// -------------------------------------------------------------- 使用者的帧上下文

struct frame
{
	::exec::static_thread_pool::scheduler _scheduler;
	::std::vector<node_sender> _roots;
	::stdexec::inplace_stop_token _stop_token;
	::std::uint64_t _index = 0;
	// 没有任何 entity 相关的成员 —— 查找能力是 `build_task` 的参数送进来的，
	// 所以 entity 手里根本没有通往 `add` / `build_all` 的路。

	frame(::exec::static_thread_pool::scheduler scheduler,
		::std::uint64_t index,
		::stdexec::inplace_stop_token stop_token = {})
		: _scheduler(scheduler)
		, _stop_token(stop_token)
		, _index(index)
	{
	}
};

// ------------------------------------------------------------------- camera
//
// 一个节点。`node_set` 是**这个 entity 自己的**结果类型：名字和方法归它，
// 成员类型 CTAD 推出来。

struct camera
{
	::std::string _name;
	bool _fail = false;

	template <class ViewSender>
	struct node_set
	{
		ViewSender _view;

		[[nodiscard]] auto view() noexcept -> auto& { return _view; }
	};
	template <class V>
	node_set(V) -> node_set<V>;

	struct compute_view
	{
		camera* _self;

		auto operator()() const -> void
		{
			if (_self->_fail)
			{
				throw ::std::runtime_error{"camera: 视锥计算失败"};
			}
			log_event("view(" + _self->_name + ")");
		}
	};

	/// 视锥：在 scheduler 上算、可取消、被多方共享所以过 `split`。
	struct make_nodes
	{
		auto operator()(camera& self, frame& context) const
		{
			return node_set{
				::stdexec::starts_on(context._scheduler, ::stdexec::just())
				| ::stdexec::then(compute_view{&self})
				// 干活的节点自己把取消令牌写进环境。结构边一律不传取消——`::exec::split` 在
				// 订阅者令牌已停止时直接 `set_stopped`、根本不启动共享体，取消要是沿结构边
				// 下压，收尾节点就一次都不跑。
				| ::stdexec::write_env(
					::stdexec::prop{::stdexec::get_stop_token, context._stop_token})
				| ::exec::split()};
		}
	};

	using nodes = task_data<make_nodes, camera&, frame&>;

	auto build_task(frame& context, entity_view<frame>, task_builder builder) -> void
	{
		log_event("build:camera");
		builder.emplace<nodes>(*this, context);
	}
};

// ------------------------------------------------------------------ renderer
//
// 两个节点 + 自有状态：一个 task 同时暴露 `begin()` 和 `fence()`，
// 消费方只查一次。

struct renderer
{
	template <class BeginSender, class FenceSender>
	struct node_set
	{
		BeginSender _begin;
		FenceSender _fence;

		[[nodiscard]] auto begin() noexcept -> auto& { return _begin; }
		[[nodiscard]] auto fence() noexcept -> auto& { return _fence; }
	};
	template <class B, class F>
	node_set(B, F) -> node_set<B, F>;

	struct open_frame
	{
		auto operator()() const -> void { log_event("begin"); }
	};

	struct close_frame
	{
		auto operator()() const -> void { log_event("fence"); }
	};

	/// 汇合点自己的状态：录制名单 + 封存标志。
	struct recorder_list
	{
		::std::vector<node_sender> _recorders;
		bool _sealed = false;

		/// @pre 汇合点尚未封存（本帧还没开始跑）。
		auto add_recorder(node_sender node) -> void
		{
			assert(!_sealed && "job arch: 录制名单已封存，注册来晚了");
			_recorders.push_back(::std::move(node));
		}
	};

	/// 排干录制名单。**在启动期才跑**，所以"谁先构建"不影响谁进得来。
	struct drain_recorders
	{
		recorder_list* _state;

		auto operator()() const
		{
			_state->_sealed = true;
			return dynamic_when_all(::std::move(_state->_recorders));
		}
	};

	struct make_nodes
	{
		auto operator()(recorder_list& state, frame& context) const
		{
			auto opening = ::stdexec::starts_on(context._scheduler, ::stdexec::just())
				| ::stdexec::then(open_frame{})
				| ::stdexec::write_env(
					::stdexec::prop{::stdexec::get_stop_token, context._stop_token})
				| ::exec::split();

			auto joining = opening
				| ::stdexec::let_value(drain_recorders{&state})
				| ::stdexec::then(close_frame{});

			return node_set{::std::move(opening), ::std::move(joining)};
		}
	};

	using nodes = stateful_task_data<make_nodes, recorder_list, frame&>;

	auto build_task(frame& context, entity_view<frame>, task_builder builder) -> void
	{
		log_event("build:renderer");
		auto&& made = builder.emplace<nodes>(context);
		context._roots.push_back(make_node(made.fence()));
	}
};

// ------------------------------------------------------------------- foliage

struct foliage
{
	::std::string _name;

	template <class RecordSender>
	struct node_set
	{
		RecordSender _record;

		[[nodiscard]] auto record() noexcept -> auto& { return _record; }
	};
	template <class R>
	node_set(R) -> node_set<R>;

	struct do_record
	{
		foliage* _self;

		auto operator()() const -> void { log_event("record(" + _self->_name + ")"); }
	};

	/// 录制：等齐全部依赖，然后录。
	struct make_nodes
	{
		auto operator()(foliage& self, ::std::vector<node_sender> dependencies) const
		{
			return node_set{
				dynamic_when_all(::std::move(dependencies)) | ::stdexec::then(do_record{&self})};
		}
	};

	using nodes = task_data<make_nodes, foliage&, ::std::vector<node_sender>>;

	auto build_task(frame&, entity_view<frame> entities, task_builder builder) -> void
	{
		log_event("build:foliage");

		auto dependencies = ::std::vector<node_sender>{};

		// —— 显式构建：先问"构建了没"，自己驱动，再从只读视图取 task ——
		if (auto camera_entity = entities.entity<camera>())
		{
			if (!camera_entity->task_built())
			{
				auto const built = camera_entity->build_task();
				if (auto* const seen = built.task<camera::nodes>())
				{
					dependencies.push_back(make_node(seen->view()));
				}
			}
			else if (auto* const seen = camera_entity->task<camera::nodes>())
			{
				dependencies.push_back(make_node(seen->view()));
			}
		}
		else
		{
			log_event("无相机");
		}

		// —— 隐式构建：直接取 task，没构建就顺手把它构建了 ——
		if (auto renderer_entity = entities.entity<renderer>())
		{
			// 一个 task 暴露多个节点，所以只查一次。
			auto* const made = renderer_entity->task<renderer::nodes>();
			assert(made != nullptr && "job arch: renderer 没有登记 nodes");

			dependencies.push_back(make_node(made->begin()));

			auto&& recording = builder.emplace<nodes>(*this, ::std::move(dependencies));
			made->add_recorder(make_node(::std::move(recording.record())));
		}
	}
};

// ---------------------------------------------------------------- 能力隔绝

// 隔绝是**类型**层面的，不是命名约定：entity 在 build_task 里拿到的 view 上，
// `add` / `build_all` 这两个名字根本不存在，手滑都调不到。
//
// 检测要经过模板形参才行：requires 表达式只对**依赖**构造做替换失败，
// 直接写 `requires (entity_view<frame> v) { v.add(x); }` 会当场硬报错而不是求值成 false。
template <class TargetType, class EntityType>
concept can_add = requires (TargetType target, EntityType& object) { target.add(object); };

template <class TargetType>
concept can_look_up = requires (TargetType target) { target.template entity<camera>(); };

static_assert(!can_add<entity_view<frame>, camera>, "entity_view 不该有 add");
static_assert(can_add<entity_storage<frame>&, camera>, "entity_storage 应该有 add");
static_assert(can_look_up<entity_view<frame>>, "entity_view 应该能查");
static_assert(!can_look_up<entity_storage<frame>&>, "entity_storage 不该负责查 entity");

// ------------------------------------------------------------------- 跑一帧

/// 把本帧的根节点扇入起来跑完。返回 false 表示整帧被取消；图内的错误以异常抛出。
///
/// 直接用 `::stdexec::sync_wait`：它的接收者环境只应答 scheduler 那几个查询、
/// **不提供 `get_stop_token`**，于是根节点拿到的是 `never_stop_token` —— 正好符合
/// "结构边不传取消"。不需要自己再写一遍等待逻辑。
auto run_frame(frame& context) -> bool
{
	return ::stdexec::sync_wait(dynamic_when_all(::std::move(context._roots))).has_value();
}

// -------------------------------------------------------------------- 场景

int g_failures = 0;

auto report(char const* title, ::std::string const& expected) -> void
{
	auto const actual = take_log();
	auto const ok = actual == expected;
	::std::printf("  %-32s %s\n", title, ok ? "ok" : "FAIL");
	if (!ok)
	{
		::std::printf("      期望: %s\n      实际: %s\n", expected.c_str(), actual.c_str());
		++g_failures;
	}
}

auto check(char const* title, bool ok) -> void
{
	::std::printf("  %-32s %s\n", title, ok ? "ok" : "FAIL");
	if (!ok)
	{
		++g_failures;
	}
}

int main()
{
	auto pool = ::exec::static_thread_pool{4};
	auto const scheduler = pool.get_scheduler();

	auto eye = camera{._name = "main"};
	auto draw = renderer{};
	auto grass = foliage{._name = "grass"};

	::std::printf("job-arch: entity / task 两层\n");

	// 1. 基本：三个 entity 都参与
	{
		auto context = frame{scheduler, 1};
		auto world = entity_storage<frame>{};
		world.add(eye);
		world.add(draw);
		world.add(grass);
		build_all(world, context);
		take_log();
		auto const finished = run_frame(context);
		check("1 基本 · 跑完没被取消", finished);
		report("1 基本 · 执行顺序", "begin view(main) record(grass) fence");
	}

	// 2. 注册顺序反过来，结果一致
	{
		auto context = frame{scheduler, 2};
		auto world = entity_storage<frame>{};
		world.add(grass);
		world.add(draw);
		world.add(eye);
		build_all(world, context);
		take_log();
		run_frame(context);
		report("2 注册顺序无关", "begin view(main) record(grass) fence");
	}

	// 3. 依赖方先被 build_all 碰到，它把被依赖方拽起来
	{
		auto context = frame{scheduler, 3};
		auto world = entity_storage<frame>{};
		world.add(grass);
		world.add(eye);
		world.add(draw);
		take_log();
		build_all(world, context);
		report("3 依赖方先构建", "build:foliage build:camera build:renderer");
		run_frame(context);
		take_log();
	}

	// 4. 被依赖方先构建：foliage 走 task_built() 那条分支
	{
		auto context = frame{scheduler, 4};
		auto world = entity_storage<frame>{};
		world.add(eye);
		world.add(draw);
		world.add(grass);
		take_log();
		build_all(world, context);
		report("4 被依赖方先构建", "build:camera build:renderer build:foliage");
		run_frame(context);
		take_log();
	}

	// 5. camera 本帧不参与：entity<camera>() 返回 nullopt，foliage 自己降级
	{
		auto context = frame{scheduler, 5};
		auto world = entity_storage<frame>{};
		world.add(draw);
		world.add(grass);
		build_all(world, context);
		take_log();
		run_frame(context);
		report("5 相机本帧不参与", "begin record(grass) fence");
	}

	// 6. 错误传播
	{
		eye._fail = true;
		auto context = frame{scheduler, 6};
		auto world = entity_storage<frame>{};
		world.add(eye);
		world.add(draw);
		world.add(grass);
		build_all(world, context);
		take_log();

		auto caught = ::std::string{"(没抛)"};
		try
		{
			run_frame(context);
		}
		catch (::std::exception const& error)
		{
			caught = error.what();
		}
		take_log();
		check("6 错误传播", caught == "camera: 视锥计算失败");
		eye._fail = false;
	}

	// 7. 整帧取消：令牌一开始就停了，干活的节点全部跳过，图照常收敛
	{
		auto source = ::stdexec::inplace_stop_source{};
		source.request_stop();

		auto context = frame{scheduler, 7, source.get_token()};
		auto world = entity_storage<frame>{};
		world.add(eye);
		world.add(draw);
		world.add(grass);
		build_all(world, context);
		take_log();

		auto const finished = run_frame(context);
		auto const seen = take_log();
		check("7 整帧取消", !finished && seen.empty());
	}

	// 8. 帧隔离：同一批 entity 连跑两帧，第二帧的 task 池是全新的
	{
		for (auto index = ::std::uint64_t{8}; index != 10; ++index)
		{
			auto context = frame{scheduler, index};
			auto world = entity_storage<frame>{};
			world.add(eye);
			world.add(draw);
			world.add(grass);
			build_all(world, context);
			take_log();
			run_frame(context);
			report(index == 8 ? "8 帧隔离 · 第一帧" : "8 帧隔离 · 第二帧",
				"begin view(main) record(grass) fence");
		}
	}

	::std::printf("%s\n", g_failures == 0 ? "全部通过" : "有失败");
	return g_failures == 0 ? 0 : 1;
}
