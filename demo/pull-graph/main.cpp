/// pull-graph 的最小 demo：视锥剔除那条链。
///
///   camera      → `view_node`         细粒度：没有帧状态，一条被多方共享的边
///   renderer    → `render_job`        粗粒度：录制名单是帧状态，两条共享边
///   visibility  → `visible_set_node`  细粒度：**跨 entity**（依赖 camera + renderer）
///   foliage     → `foliage_job`       粗粒度：`_staging` 写在录制、读在清理
///
/// 两种粒度用的是同一个 `get<T>(owner)`，库不区分。划分依据只有一条：
/// **帧状态耦不耦合**。
///
/// 值能走值通道的就走（view / command / 可见集都是节点的产出），走不了的才用状态——
/// `_staging` 走不了，因为录制节点被推进了擦除过的名单，值通道到那儿就断了。
///
/// 名单里只需要放"自己要往帧根挂东西"的 entity。camera 和 visibility 是纯提供方，
/// 由依赖方拉起来。

#include <atomic>
#include <cstdint>
#include <exception>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <exec/split.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "pull_graph.h"

namespace pg = ::bvn::pull_graph;

namespace
{

int g_failures = 0;

auto check(bool condition, ::std::string_view what) -> void
{
	::std::println("  [{}] {}", condition ? "ok  " : "FAIL", what);
	if (!condition)
	{
		++g_failures;
	}
}

auto section(::std::string_view title) -> void
{
	::std::println("");
	::std::println("{}", title);
}

// ------------------------------------------------------------ 使用者的上下文

struct context : pg::frame_context<context>
{
	::exec::static_thread_pool::scheduler _scheduler;
	::std::uint64_t _index = 0;

	context(
		::exec::static_thread_pool::scheduler scheduler,
		::std::uint64_t index,
		::stdexec::inplace_stop_token stop_token = {}) noexcept
		: _scheduler(scheduler)
		, _index(index)
	{
		_stop_token = stop_token;
	}
};

// -------------------------------------------------------------------- camera

struct camera
{
	::std::atomic<int> _materialized{0};
	::std::atomic<int> _views{0};

	auto compute_view(::std::uint64_t frame) -> int
	{
		++_views;
		return static_cast<int>(frame) + 100;
	}
};

[[nodiscard]] auto make_view(camera& self, context& target)
{
	return ::exec::split(::stdexec::write_env(
		::stdexec::starts_on(target._scheduler, ::stdexec::just())
			| ::stdexec::then([&self, &target] { return self.compute_view(target._index); }),
		::stdexec::prop{::stdexec::get_stop_token, target._stop_token}));
}

/// 细粒度：一条边、没有帧状态、被多方共享 → 自己一个条目。
struct view_node
{
	decltype(make_view(::std::declval<camera&>(), ::std::declval<context&>())) _sender;

	view_node(camera& self, context& target)
		: _sender(make_view(self, target))
	{
		++self._materialized;
	}
};

// ------------------------------------------------------------------ renderer

struct renderer
{
	::std::atomic<int> _begins{0};
	::std::atomic<int> _submits{0};
	::std::atomic<int> _fences{0};

	auto open_command_buffer(::std::uint64_t frame) -> int
	{
		++_begins;
		return static_cast<int>(frame) + 200;
	}

	auto submit(int command) -> void
	{
		static_cast<void>(command);
		++_submits;
	}

	auto wait_fence() -> void
	{
		++_fences;
	}
};

[[nodiscard]] auto make_begin(renderer& self, context& target)
{
	return ::exec::split(::stdexec::write_env(
		::stdexec::starts_on(target._scheduler, ::stdexec::just())
			| ::stdexec::then([&self, &target] { return self.open_command_buffer(target._index); }),
		::stdexec::prop{::stdexec::get_stop_token, target._stop_token}));
}

using begin_sender = decltype(make_begin(::std::declval<renderer&>(), ::std::declval<context&>()));

/// 提交 + 帧末等 fence。三条完成路径**都**要落到 `wait_fence` 上——少任何一条，
/// 取消或出错时资源就没人回收。这条边**不可取消**（没有 `write_env`），
/// 取消是从 `begin` 那边咬住的。
[[nodiscard]] auto make_fence(
	renderer& self,
	context& target,
	begin_sender const& begin,
	::std::vector<pg::node_sender>& recorders,
	bool& sealed)
{
	return ::exec::split(begin
		// 取名单放在 `let_value` 的体里：体是启动之后才跑的，于是"读名单"落在整张图
		// 物化完毕之后，谁先挂谁后挂彻底无关。这一句是 push 侧顺序无关的支点。
		| ::stdexec::let_value([&recorders, &sealed](int const&)
			{
				sealed = true;
				return pg::dynamic_when_all(::std::move(recorders));
			})
		| ::stdexec::continues_on(target._scheduler)
		| ::stdexec::then([&self, &target] { self.submit(static_cast<int>(target._index)); })
		| ::stdexec::let_error([&self](::std::exception_ptr error)
			{
				self.wait_fence();
				return ::stdexec::just_error(::std::move(error));
			})
		| ::stdexec::let_stopped([&self]
			{
				self.wait_fence();
				return ::stdexec::just_stopped();
			})
		| ::stdexec::then([&self] { self.wait_fence(); }));
}

using fence_sender = decltype(make_fence(
	::std::declval<renderer&>(),
	::std::declval<context&>(),
	::std::declval<begin_sender const&>(),
	::std::declval<::std::vector<pg::node_sender>&>(),
	::std::declval<bool&>()));

/// 粗粒度：录制名单是帧状态（别人 push、启动期才取走），走不了值通道，
/// 所以它和两条共享边住在一个条目里 —— 兄弟之间是**成员访问**，不是表查找。
struct render_job
{
	// 声明序有意义：`_fence` 的表达式攥着上面几个的地址。
	::std::vector<pg::node_sender> _recorders;
	bool _sealed = false;
	begin_sender _begin;
	fence_sender _fence;

	render_job(renderer& self, context& target)
		: _begin(make_begin(self, target))
		, _fence(make_fence(self, target, _begin, _recorders, _sealed))
	{
		// 没有 build()：构造函数里就把根节点挂上。
		target.add(pg::make_node(_fence));
	}

	render_job(render_job const&) = delete;
	auto operator=(render_job const&) -> render_job& = delete;

	/// 库只给裸容器，"挂晚了要响亮地失败"是使用者自己的选择，就这一行。
	auto add_recorder(pg::node_sender node) -> void
	{
		if (_sealed)
		{
			throw ::std::logic_error{"renderer: 录制名单已封存"};
		}

		_recorders.push_back(::std::move(node));
	}
};

// ---------------------------------------------------------------- visibility

struct visibility
{
	camera& _camera;
	renderer& _renderer;
	::std::atomic<int> _materialized{0};
	::std::atomic<int> _culls{0};

	auto cull(int view) -> int
	{
		++_culls;
		return view / 10;
	}
};

[[nodiscard]] auto make_visible_set(visibility& self, context& target, view_node& view, render_job& render)
{
	return ::exec::split(::stdexec::when_all(view._sender, render._begin)
		| ::stdexec::continues_on(target._scheduler)
		| ::stdexec::then([&self](int const& seen, int const& command)
			{
				static_cast<void>(command);
				return self.cull(seen);
			}));
}

/// 细粒度：表达式**跨了两个 entity**，但自己没有帧状态 → 自己一个条目。
/// 这正是"共享 split 依赖别的 job"那个场景；`get` 把依赖当场物化出来，
/// 所以它是构造函数里的一个普通成员，不需要任何惰性备忘格。
struct visible_set_node
{
	decltype(make_visible_set(
		::std::declval<visibility&>(),
		::std::declval<context&>(),
		::std::declval<view_node&>(),
		::std::declval<render_job&>())) _sender;

	visible_set_node(visibility& self, context& target)
		: _sender(make_visible_set(self, target,
			target.get<view_node>(self._camera),
			target.get<render_job>(self._renderer)))
	{
		++self._materialized;
	}
};

// ------------------------------------------------------------------- foliage

struct foliage
{
	camera& _camera;
	renderer& _renderer;
	visibility& _visibility;
	::std::string _name;
	bool _visible = true;
	bool _fail = false;
	::std::atomic<int> _records{0};
	::std::atomic<int> _releases{0};

	auto record(int count) -> int
	{
		++_records;
		return count + 1;
	}

	auto release(int staging) -> void
	{
		static_cast<void>(staging);
		++_releases;
	}
};

/// 粗粒度：`_staging` 写在录制、读在清理。它走不了值通道——录制节点被推进了
/// 擦除过的名单（`node_sender` 不传值），清理又挂在 fence 后面。
struct foliage_job
{
	foliage& _foliage;
	context& _context;
	int _staging = 0;

	foliage_job(foliage& self, context& target)
		: _foliage(self)
		, _context(target)
	{
		auto&& render = target.get<render_job>(self._renderer);

		// 本帧不参与就一条都不挂。
		if (self._visible)
		{
			render.add_recorder(_record_node());
		}

		// 清理只有一个消费者（帧根），所以不 `split`、也不用写出类型。
		target.add(pg::make_node(render._fence
			| ::stdexec::then([this] { _foliage.release(_staging); })));
	}

	foliage_job(foliage_job const&) = delete;
	auto operator=(foliage_job const&) -> foliage_job& = delete;

	/// 唯一消费者（录制名单），所以不是 `split`；不是 `split` 就**必须私有**——
	/// `node_sender` 单发射，第二个调用方就是第二次执行。
	[[nodiscard]] auto _record_node() -> pg::node_sender
	{
		auto&& visible = _context.get<visible_set_node>(_foliage._visibility);
		auto&& view = _context.get<view_node>(_foliage._camera);

		return pg::make_node(::stdexec::when_all(visible._sender, view._sender)
			// 这个 `continues_on` 不是可选的：名单是启动期才读的，读的时候上游早就
			// 完成了，订阅一个已完成的 `split` 会原地同步派发——所有录制者会串在
			// 同一根线程上。换一次调度才真并行。
			| ::stdexec::continues_on(_context._scheduler)
			| ::stdexec::then([this](int const& count, int const& seen)
				{
					static_cast<void>(seen);

					if (_foliage._fail)
					{
						throw ::std::runtime_error{_foliage._name + ": 录制失败"};
					}

					_staging = _foliage.record(count);
				}));
	}
};

// -------------------------------------------------------- 调用者自己的名单

/// entity 名单是**调用者的事**，库不管。两个指针，不分配。
struct entity_ref
{
	void* _object = nullptr;
	void (*_materialize)(void*, context&) = nullptr;

	/// `entry_type` 是这个 entity 的入口条目。
	template <class entry_type, class entity_type>
	[[nodiscard]] static auto of(entity_type& entity) noexcept -> entity_ref
	{
		return entity_ref{
			._object = &entity,
			._materialize = [](void* object, context& target)
				{ static_cast<void>(target.get<entry_type>(*static_cast<entity_type*>(object))); },
		};
	}

	auto materialize(context& target) const -> void
	{
		_materialize(_object, target);
	}
};

/// 一帧：造上下文 → 逐个物化名单里的入口（依赖会自己被拉起来）→ 启动。
/// 上下文一析构，本帧全部条目逆序销毁——帧隔离是结构性的。
auto run_one_frame(
	::std::vector<entity_ref> const& roster,
	::exec::static_thread_pool& pool,
	::std::uint64_t index,
	::stdexec::inplace_stop_token stop_token = {}) -> bool
{
	auto target = context{pool.get_scheduler(), index, stop_token};

	for (auto&& entity : roster)
	{
		entity.materialize(target);
	}

	return pg::run_frame(target);
}

// ------------------------------------------------------------------- 场景

struct world
{
	::exec::static_thread_pool _pool{4};
	camera _camera{};
	renderer _renderer{};
	visibility _visibility{._camera = _camera, ._renderer = _renderer};

	[[nodiscard]] auto make_foliage(::std::string name) -> foliage
	{
		return foliage{
			._camera = _camera,
			._renderer = _renderer,
			._visibility = _visibility,
			._name = ::std::move(name),
		};
	}
};

auto scenario_basic() -> void
{
	section("1. 基本：renderer + 两株 foliage（camera / visibility 由依赖方拉起来）");

	auto stage = world{};
	auto grass = stage.make_foliage("grass");
	auto trees = stage.make_foliage("trees");

	auto roster = ::std::vector<entity_ref>{
		entity_ref::of<render_job>(stage._renderer),
		entity_ref::of<foliage_job>(grass),
		entity_ref::of<foliage_job>(trees),
	};

	check(run_one_frame(roster, stage._pool, 0), "整帧正常完成");
	check(stage._camera._materialized == 1, "camera 的边只物化一次（两株植被 + 剔除都要它）");
	check(stage._camera._views == 1, "视图只算一次");
	check(stage._visibility._culls == 1, "剔除只跑一次（共享的 split）");
	check(stage._renderer._begins == 1, "begin 只跑一次");
	check(grass._records == 1 && trees._records == 1, "两株各录制一次");
	check(stage._renderer._submits == 1, "提交一次");
	check(stage._renderer._fences == 1, "等 fence 一次");
	check(grass._releases == 1 && trees._releases == 1, "两株各清理一次");
}

auto scenario_order() -> void
{
	section("2. 名单顺序无关（renderer 排最前 vs 排最后）");

	auto measure = [](bool renderer_first)
	{
		auto stage = world{};
		auto grass = stage.make_foliage("grass");

		auto roster = renderer_first
			? ::std::vector<entity_ref>{
				entity_ref::of<render_job>(stage._renderer), entity_ref::of<foliage_job>(grass)}
			: ::std::vector<entity_ref>{
				entity_ref::of<foliage_job>(grass), entity_ref::of<render_job>(stage._renderer)};

		auto const completed = run_one_frame(roster, stage._pool, 0);
		return ::std::tuple{completed, stage._renderer._submits.load(), grass._records.load()};
	};

	auto const [ok_first, submit_first, record_first] = measure(true);
	auto const [ok_last, submit_last, record_last] = measure(false);

	check(ok_first && ok_last, "两种顺序都正常完成");
	check(submit_first == 1 && submit_last == 1, "都只提交一次");
	check(record_first == 1 && record_last == 1, "都只录制一次");
}

auto scenario_laziness() -> void
{
	section("3. 惰性物化：没人要的东西一次都不造");

	auto stage = world{};
	auto roster = ::std::vector<entity_ref>{entity_ref::of<render_job>(stage._renderer)};

	check(run_one_frame(roster, stage._pool, 0), "只有 renderer 在场时整帧正常完成");
	check(stage._camera._materialized == 0, "camera 的边一次都没物化");
	check(stage._visibility._materialized == 0, "剔除的边一次都没物化");
	check(stage._renderer._submits == 1, "空名单照常提交");
	check(stage._renderer._fences == 1, "照常等 fence");
}

auto scenario_conditional() -> void
{
	section("4. 条件性参与（本帧被剔除的植被）");

	auto stage = world{};
	auto grass = stage.make_foliage("grass");
	grass._visible = false;

	auto roster = ::std::vector<entity_ref>{
		entity_ref::of<render_job>(stage._renderer),
		entity_ref::of<foliage_job>(grass),
	};

	check(run_one_frame(roster, stage._pool, 0), "整帧正常完成");
	check(grass._records == 0, "没有录制");
	check(stage._visibility._materialized == 0, "剔除的边根本没被构建（不挂就不拉）");
	check(stage._renderer._submits == 1, "照常提交");
	check(grass._releases == 1, "帧末清理仍然执行");
}

auto scenario_failure() -> void
{
	section("5. 错误传播（一株录制失败）");

	auto stage = world{};
	auto grass = stage.make_foliage("grass");
	auto trees = stage.make_foliage("trees");
	trees._fail = true;

	auto roster = ::std::vector<entity_ref>{
		entity_ref::of<render_job>(stage._renderer),
		entity_ref::of<foliage_job>(grass),
		entity_ref::of<foliage_job>(trees),
	};

	auto message = ::std::string{};
	try
	{
		static_cast<void>(run_one_frame(roster, stage._pool, 0));
	}
	catch (::std::exception const& error)
	{
		message = error.what();
	}

	check(message == "trees: 录制失败", ::std::string{"异常传到调用者："} + message);
	check(stage._renderer._submits == 0, "提交被跳过");
	check(stage._renderer._fences == 1, "**fence 仍然执行**（错误路径）");
	check(grass._records == 1, "不广播取消，兄弟照样录完");
	check(grass._releases == 0, "下游清理没有在失败时被误跑");
}

auto scenario_cancel() -> void
{
	section("6. 整帧取消（上层 request_stop）");

	auto stage = world{};
	auto grass = stage.make_foliage("grass");

	auto roster = ::std::vector<entity_ref>{
		entity_ref::of<render_job>(stage._renderer),
		entity_ref::of<foliage_job>(grass),
	};

	auto source = ::stdexec::inplace_stop_source{};
	source.request_stop();

	check(!run_one_frame(roster, stage._pool, 0, source.get_token()), "整帧报告为已取消");
	check(stage._renderer._begins == 0, "begin 没跑（取消从 `write_env` 那条路咬住）");
	check(stage._camera._views == 0, "视图没算");
	check(grass._records == 0, "没有录制");
	check(stage._renderer._submits == 0, "没有提交");
	check(stage._renderer._fences == 1, "**fence 仍然执行**（取消路径）");
}

auto scenario_cycle() -> void
{
	section("7. 物化期成环");

	struct knot
	{
		knot* _next = nullptr;
	};

	struct knot_job
	{
		knot_job(knot& self, context& target)
		{
			if (self._next != nullptr)
			{
				static_cast<void>(target.get<knot_job>(*self._next));
			}
		}
	};

	auto pool = ::exec::static_thread_pool{2};
	auto left = knot{};
	auto right = knot{};
	left._next = &right;
	right._next = &left;

	auto message = ::std::string{};
	try
	{
		auto target = context{pool.get_scheduler(), 0};
		static_cast<void>(target.get<knot_job>(left));
	}
	catch (::std::exception const& error)
	{
		message = error.what();
	}

	check(message == "pull graph: 物化期依赖成环", ::std::string{"成环被当场抓住："} + message);

	auto self_message = ::std::string{};
	auto lone = knot{};
	lone._next = &lone;
	try
	{
		auto target = context{pool.get_scheduler(), 0};
		static_cast<void>(target.get<knot_job>(lone));
	}
	catch (::std::exception const& error)
	{
		self_message = error.what();
	}

	check(self_message == "pull graph: 物化期依赖成环", "自环也一样");
}

auto scenario_frames() -> void
{
	section("8. 帧间隔离（连续 3 帧，中途增删）");

	auto stage = world{};
	auto grass = stage.make_foliage("grass");
	auto guest = stage.make_foliage("guest");

	auto ok = true;
	for (auto index = ::std::uint64_t{0}; index != 3; ++index)
	{
		auto roster = ::std::vector<entity_ref>{
			entity_ref::of<render_job>(stage._renderer),
			entity_ref::of<foliage_job>(grass),
		};

		if (index == 1)
		{
			roster.push_back(entity_ref::of<foliage_job>(guest));
		}

		ok = run_one_frame(roster, stage._pool, index) && ok;
	}

	check(ok, "三帧都正常完成");
	check(stage._renderer._submits == 3 && stage._renderer._fences == 3, "每帧各提交 / 等 fence 一次");
	check(stage._camera._materialized == 3, "每帧一套全新的条目");
	check(grass._records == 3, "常驻植被每帧都录");
	check(guest._records == 1, "临时植被只在它在场的那帧录");
}

}

auto main() -> int
{
	::std::println("pull-graph 最小 demo");

	scenario_basic();
	scenario_order();
	scenario_laziness();
	scenario_conditional();
	scenario_failure();
	scenario_cancel();
	scenario_cycle();
	scenario_frames();

	::std::println("");
	if (g_failures == 0)
	{
		::std::println("全部通过。");
		return 0;
	}

	::std::println("{} 项失败。", g_failures);
	return 1;
}
