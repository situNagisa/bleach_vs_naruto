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
#include <semaphore>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
	entities<frame> _entities;

	frame(::exec::static_thread_pool::scheduler scheduler,
		::std::uint64_t index,
		::stdexec::inplace_stop_token stop_token = {})
		: _scheduler(scheduler)
		, _stop_token(stop_token)
		, _index(index)
		, _entities(*this)
	{
	}
};

// ------------------------------------------------------------- 两种节点形状

/// 干活的节点自己把取消令牌写进环境。结构边一律不传取消——`::exec::split` 在订阅者
/// 令牌已停止时直接 `set_stopped`、根本不启动共享体，取消要是沿结构边下压，收尾节点
/// 就一次都不跑。
template <class Sender>
[[nodiscard]] auto cancellable(Sender sender, ::stdexec::inplace_stop_token token)
{
	return ::stdexec::write_env(::std::move(sender), ::stdexec::prop{::stdexec::get_stop_token, token});
}

/// "在 scheduler 上跑一段活、可取消、可被多方共享"——帧图里最常见的节点形状。
///
/// 拆成**别名 + 工厂**而不是一个推导返回类型的工厂，是因为 task 嵌套在 entity 里
/// （`camera::view`）：声明 `_sender` 成员的那一刻外层 entity 还没闭合，
/// 而 `decltype(推导返回类型的函数(...))` 要求那个函数**已经定义完**。别名只依赖
/// 函数对象的类型，绕开了这层循环。
template <class Functor>
using shared_node = decltype(cancellable(
	::stdexec::then(
		::stdexec::starts_on(
			::std::declval<::exec::static_thread_pool::scheduler>(), ::stdexec::just()),
		::std::declval<Functor>()),
	::std::declval<::stdexec::inplace_stop_token>()));

template <class Functor>
[[nodiscard]] auto make_shared_node(frame& context, Functor functor) -> shared_node<Functor>
{
	return cancellable(
		::stdexec::then(
			::stdexec::starts_on(context._scheduler, ::stdexec::just()), ::std::move(functor)),
		context._stop_token);
}

/// 共享节点要过一道 `::exec::split`：本帧只跑一次，每个消费者拿一份拷贝。
template <class Functor>
using split_node = decltype(::exec::split(::std::declval<shared_node<Functor>>()));

template <class Functor>
[[nodiscard]] auto make_split_node(frame& context, Functor functor) -> split_node<Functor>
{
	return ::exec::split(make_shared_node(context, ::std::move(functor)));
}

/// 一个"名单在运行期才定"的扇入。从工厂反推类型，别自己拼——`dynamic_when_all` 存的是
/// `::std::views::all_t<...>`（右值容器进来会包成 `owning_view`），写死会跟它失联。
using recorder_join = decltype(dynamic_when_all(::std::declval<::std::vector<node_sender>>()));

// ------------------------------------------------------------------- camera

struct camera
{
	::std::string _name;
	bool _fail = false;

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

	/// 视锥：被多方共享，所以过 `split`——本帧只算一次。
	struct view
	{
		split_node<compute_view> _sender;

		view(camera& self, frame& context)
			: _sender(make_split_node(context, compute_view{&self}))
		{
		}

		view(view&&) = delete;
		auto operator=(view&&) -> view& = delete;
	};

	auto build_task(frame& context, task_builder builder) -> void
	{
		log_event("build:camera");
		builder.emplace<view>(*this, context);
	}
};

// ------------------------------------------------------------------ renderer

struct renderer
{
	struct open_frame
	{
		auto operator()() const -> void { log_event("begin"); }
	};

	/// 开帧：录制者都挂在它后面，所以也是共享的。
	struct begin
	{
		split_node<open_frame> _sender;

		explicit begin(frame& context)
			: _sender(make_split_node(context, open_frame{}))
		{
		}

		begin(begin&&) = delete;
		auto operator=(begin&&) -> begin& = delete;
	};

	/// 排干录制名单。**在启动期才跑**，所以"谁先构建"不影响谁进得来。
	struct drain_recorders
	{
		::std::vector<node_sender>* _recorders;
		bool* _sealed;

		auto operator()() const -> recorder_join
		{
			*_sealed = true;
			return dynamic_when_all(::std::move(*_recorders));
		}
	};

	struct close_frame
	{
		auto operator()() const -> void { log_event("fence"); }
	};

	struct fence
	{
		::std::vector<node_sender> _recorders;
		bool _sealed = false;
		decltype(::stdexec::then(
			::stdexec::let_value(
				::std::declval<split_node<open_frame>>(), ::std::declval<drain_recorders>()),
			::std::declval<close_frame>())) _sender;

		explicit fence(begin& opening)
			: _sender(::stdexec::then(
				::stdexec::let_value(opening._sender, drain_recorders{&_recorders, &_sealed}),
				close_frame{}))
		{
		}

		// 地址敏感：`_sender` 里攥着 `_recorders` / `_sealed` 的地址。
		// 顺带也躲开 `::entt::basic_any` 去实例化 `::std::vector` 那条永远声明着、
		// 但对只可移动元素不可用的拷贝构造。
		fence(fence&&) = delete;
		auto operator=(fence&&) -> fence& = delete;

		/// @pre 汇合点尚未封存（本帧还没开始跑）。
		auto add_recorder(node_sender node) -> void
		{
			assert(!_sealed && "job arch: 录制名单已封存，注册来晚了");
			_recorders.push_back(::std::move(node));
		}
	};

	auto build_task(frame& context, task_builder builder) -> void
	{
		log_event("build:renderer");
		auto&& opening = builder.emplace<begin>(context);
		auto&& joining = builder.emplace<fence>(opening);
		context._roots.push_back(make_node(joining._sender));
	}
};

// ------------------------------------------------------------------- foliage

struct foliage
{
	::std::string _name;

	struct do_record
	{
		foliage* _self;

		auto operator()() const -> void { log_event("record(" + _self->_name + ")"); }
	};

	struct record
	{
		decltype(::stdexec::then(
			::std::declval<recorder_join>(), ::std::declval<do_record>())) _sender;

		record(foliage& self, ::std::vector<node_sender> dependencies)
			: _sender(::stdexec::then(dynamic_when_all(::std::move(dependencies)), do_record{&self}))
		{
		}

		record(record&&) = delete;
		auto operator=(record&&) -> record& = delete;
	};

	auto build_task(frame& context, task_builder builder) -> void
	{
		log_event("build:foliage");

		auto dependencies = ::std::vector<node_sender>{};

		// —— 显式构建：先问"构建了没"，自己驱动，再从只读视图取 task ——
		if (auto camera_entity = context._entities.entity<camera>())
		{
			if (!camera_entity->task_built())
			{
				auto const built = camera_entity->build_task();
				if (auto* const seen = built.task<camera::view>())
				{
					dependencies.push_back(make_node(seen->_sender));
				}
			}
			else if (auto* const seen = camera_entity->task<camera::view>())
			{
				dependencies.push_back(make_node(seen->_sender));
			}
		}
		else
		{
			log_event("无相机");
		}

		// —— 隐式构建：直接取 task，没构建就顺手把它构建了 ——
		if (auto renderer_entity = context._entities.entity<renderer>())
		{
			if (auto* const opening = renderer_entity->task<renderer::begin>())
			{
				dependencies.push_back(make_node(opening->_sender));
			}

			auto&& recording = builder.emplace<record>(*this, ::std::move(dependencies));

			if (auto* const joining = renderer_entity->task<renderer::fence>())
			{
				joining->add_recorder(make_node(::std::move(recording._sender)));
			}
		}
	}
};

// ------------------------------------------------------------------- 跑一帧

struct run_state
{
	::std::binary_semaphore _done{0};
	::std::exception_ptr _error;
	bool _stopped = false;
};

struct run_receiver
{
	using receiver_concept = ::stdexec::receiver_t;

	run_state* _state;

	auto set_value() noexcept -> void { _state->_done.release(); }

	auto set_error(::std::exception_ptr error) noexcept -> void
	{
		_state->_error = ::std::move(error);
		_state->_done.release();
	}

	auto set_stopped() noexcept -> void
	{
		_state->_stopped = true;
		_state->_done.release();
	}

	/// 默认构造的令牌永不停止——结构边不传取消。
	[[nodiscard]] auto get_env() const noexcept -> node_env { return node_env{}; }
};

/// 手写的 `sync_wait`，只为了给根接收者一个永不停止的令牌。
/// 返回 false 表示整帧被取消；图内的错误以异常抛出。
auto run_frame(frame& context) -> bool
{
	auto state = run_state{};
	auto operation = ::stdexec::connect(
		dynamic_when_all(::std::move(context._roots)), run_receiver{&state});
	::stdexec::start(operation);
	state._done.acquire();

	if (state._error)
	{
		::std::rethrow_exception(state._error);
	}
	return !state._stopped;
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
		context._entities.add(eye);
		context._entities.add(draw);
		context._entities.add(grass);
		context._entities.build_all();
		take_log();
		auto const finished = run_frame(context);
		check("1 基本 · 跑完没被取消", finished);
		report("1 基本 · 执行顺序", "begin view(main) record(grass) fence");
	}

	// 2. 注册顺序反过来，结果一致
	{
		auto context = frame{scheduler, 2};
		context._entities.add(grass);
		context._entities.add(draw);
		context._entities.add(eye);
		context._entities.build_all();
		take_log();
		run_frame(context);
		report("2 注册顺序无关", "begin view(main) record(grass) fence");
	}

	// 3. 依赖方先被 build_all 碰到，它把被依赖方拽起来
	{
		auto context = frame{scheduler, 3};
		context._entities.add(grass);
		context._entities.add(eye);
		context._entities.add(draw);
		take_log();
		context._entities.build_all();
		report("3 依赖方先构建", "build:foliage build:camera build:renderer");
		run_frame(context);
		take_log();
	}

	// 4. 被依赖方先构建：foliage 走 task_built() 那条分支
	{
		auto context = frame{scheduler, 4};
		context._entities.add(eye);
		context._entities.add(draw);
		context._entities.add(grass);
		take_log();
		context._entities.build_all();
		report("4 被依赖方先构建", "build:camera build:renderer build:foliage");
		run_frame(context);
		take_log();
	}

	// 5. camera 本帧不参与：entity<camera>() 返回 nullopt，foliage 自己降级
	{
		auto context = frame{scheduler, 5};
		context._entities.add(draw);
		context._entities.add(grass);
		context._entities.build_all();
		take_log();
		run_frame(context);
		report("5 相机本帧不参与", "begin record(grass) fence");
	}

	// 6. 错误传播
	{
		eye._fail = true;
		auto context = frame{scheduler, 6};
		context._entities.add(eye);
		context._entities.add(draw);
		context._entities.add(grass);
		context._entities.build_all();
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
		context._entities.add(eye);
		context._entities.add(draw);
		context._entities.add(grass);
		context._entities.build_all();
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
			context._entities.add(eye);
			context._entities.add(draw);
			context._entities.add(grass);
			context._entities.build_all();
			take_log();
			run_frame(context);
			report(index == 8 ? "8 帧隔离 · 第一帧" : "8 帧隔离 · 第二帧",
				"begin view(main) record(grass) fence");
		}
	}

	::std::printf("%s\n", g_failures == 0 ? "全部通过" : "有失败");
	return g_failures == 0 ? 0 : 1;
}
