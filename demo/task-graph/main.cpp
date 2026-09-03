/// task-graph 原语的自足冒烟测试。
///
/// 这里没有 renderer、没有 GPU、没有任何具体领域——只有三种 entity，用来把库里的
/// 原语各钉一遍：
///
///   `worker`   纯任务：`build()` 往帧上下文挂一条根节点。谁也不依赖。
///   `gate`     共享节点的提供者：`_work` 和 `_done` 都是 `::exec::split`，
///              还带一个 `::std::vector<node_sender>` 让别人往里挂；`_done` 的体里
///              取走名单交给 `dynamic_when_all` 汇合。
///   `follower` 依赖 `gate`：往它的名单里挂一条，再往帧根挂一条"等 gate 收尾后清理"。
///
/// 三种 entity **都不持有自己的 job**——job 交给 `frame_context` 托管，
/// follower 用 `context.job_for<gate_job>(&gate)` 去取本帧的 gate job。
///
/// 钉住的性质：注册顺序无关、条件性参与、空名单、错误传播、整帧取消时收尾仍然执行、
/// 迟到注册是响亮的异常（使用者自己防的）、帧与帧之间隔离。

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <exec/split.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "task_graph.h"

namespace tg = ::bvn::task_graph;

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

/// 库的 `frame_context` 只管 job / 根节点 / 取消；调度器和帧号是**使用者的帧数据**，
/// 派生一层加上去即可。`run_frame` 只认基类那部分。
struct context : tg::frame_context
{
	::exec::static_thread_pool::scheduler _scheduler;
	::std::uint64_t _index = 0;

	context(::exec::static_thread_pool::scheduler scheduler, ::std::uint64_t index) noexcept
		: _scheduler(scheduler)
		, _index(index)
	{}
};

/// 取消直接用 stdexec 自己的 `write_env`，库不包一层。
template <class sender_type>
[[nodiscard]] auto cancellable_by(sender_type sender, context const& target)
{
	return ::stdexec::write_env(
		::std::move(sender),
		::stdexec::prop{::stdexec::get_stop_token, target.stop_token()});
}

// ------------------------------------------------------------------- worker

/// 纯任务 entity：跟别人没有任何关系，挂一条根节点就完事。
struct worker
{
	struct job
	{
		worker& _worker;
		context& _context;

		auto build() -> void
		{
			_context.add(tg::make_node(cancellable_by(
				::stdexec::starts_on(_context._scheduler, ::stdexec::just())
					| ::stdexec::then([this] { ++_worker._runs; }),
				_context)));
		}
	};

	::std::atomic<int> _runs{0};

	/// 阶段 A：造好 job 交给上下文托管，key 是自己的地址。自己不留一份。
	auto frame_job(context& target) -> void
	{
		target.add_job<job>(this, *this, target);
	}
};

// --------------------------------------------------------------------- gate

/// gate 的跨帧数据。节点工厂要引用它，所以它必须是先定义好的完整类型。
struct gate_data
{
	::std::atomic<int> _work_runs{0};
	::std::atomic<int> _done_runs{0};
};

/// `_work`：别人要依赖它，所以必须 `split`——不然每个消费者各跑一遍。
///
/// 它是**可取消**的：取消要在这里咬住，而不是沿结构边压下去。
[[nodiscard]] auto make_work_node(context& target, gate_data& data)
{
	return ::exec::split(cancellable_by(
		::stdexec::starts_on(target._scheduler, ::stdexec::just())
			| ::stdexec::then([&data] { ++data._work_runs; }),
		target));
}

using work_node_t = decltype(make_work_node(::std::declval<context&>(), ::std::declval<gate_data&>()));

/// `_done`：等齐名单里所有人，然后收尾。三条完成路径**都**要落到收尾上——
/// 少任何一条，取消或出错时清理就没了。
///
/// 取名单放在 `let_value` 的体里：体是启动之后才跑的，于是"读名单"落在整张图
/// 构建完毕之后，谁先挂谁后挂彻底无关。
[[nodiscard]] auto make_done_node(
	context& target,
	gate_data& data,
	work_node_t const& work,
	::std::vector<tg::node_sender>& followers,
	bool& sealed)
{
	return ::exec::split(
		work
		| ::stdexec::let_value([&followers, &sealed]
			{
				sealed = true;
				return tg::dynamic_when_all(::std::move(followers));
			})
		| ::stdexec::continues_on(target._scheduler)
		| ::stdexec::let_error([&data](::std::exception_ptr error)
			{
				++data._done_runs;
				return ::stdexec::just_error(::std::move(error));
			})
		| ::stdexec::let_stopped([&data]
			{
				++data._done_runs;
				return ::stdexec::just_stopped();
			})
		| ::stdexec::then([&data] { ++data._done_runs; }));
}

using done_node_t = decltype(make_done_node(
	::std::declval<context&>(),
	::std::declval<gate_data&>(),
	::std::declval<work_node_t const&>(),
	::std::declval<::std::vector<tg::node_sender>&>(),
	::std::declval<bool&>()));

/// 本帧的 gate。两个 `split` 直接是成员——没有备忘格，构造时就建好。
///
/// 之所以能在构造期建，是因为这两条表达式**只引用自己**。引用别的 job 的节点不行，
/// 那种必须等到阶段 B（`follower::job::_follow_node` 就是）。
struct gate_job
{
	gate_data& _data;
	context& _context;

	// 声明序有意义：`_done` 的表达式攥着 `_followers` / `_sealed` 的地址。
	::std::vector<tg::node_sender> _followers;
	bool _sealed = false;
	work_node_t _work;
	done_node_t _done;

	gate_job(gate_data& data, context& target)
		: _data(data)
		, _context(target)
		, _work(make_work_node(target, data))
		, _done(make_done_node(target, data, _work, _followers, _sealed))
	{}

	gate_job(gate_job const&) = delete;
	auto operator=(gate_job const&) -> gate_job& = delete;

	/// 阶段 B：把自己的收尾节点挂进帧根。
	auto build() -> void
	{
		_context.add(tg::make_node(_done));
	}

	/// 公开的节点入口只有这两个，而且都过了 `split`——这是硬规矩：
	/// `node_sender` 单发射，没 `split` 的节点被两个人拿到就是跑两遍。
	[[nodiscard]] auto work_node() -> tg::node_sender
	{
		return tg::make_node(_work);
	}

	[[nodiscard]] auto done_node() -> tg::node_sender
	{
		return tg::make_node(_done);
	}

	/// 库只给裸容器，"挂晚了要响亮地失败"是**使用者自己的选择**，就这一行。
	auto add_follower(tg::node_sender node) -> void
	{
		if (_sealed)
		{
			throw ::std::logic_error{"gate: 名单已封存，注册来晚了"};
		}

		_followers.push_back(::std::move(node));
	}
};

struct gate
{
	gate_data _data;

	auto frame_job(context& target) -> void
	{
		target.add_job<gate_job>(this, _data, target);
	}
};

// ----------------------------------------------------------------- follower

struct follower
{
	struct job
	{
		follower& _follower;
		context& _context;

		auto build() -> void
		{
			// 阶段 B：所有 job 都已就位，这里才去取"本帧的 gate job"。
			// key 是那个 gate entity 的地址——依赖仍是具体类型直连，不查 tag 表。
			auto&& hub = _context.job_for<gate_job>(&_follower._gate);

			// 跟 gate 的同步全在这两行里：往它的名单挂一条，再往帧根挂一条清理。
			// 本帧不参与就一条都不挂。
			if (_follower._active)
			{
				hub.add_follower(_follow_node(hub));
			}

			_context.add(tg::make_node(hub.done_node()
				| ::stdexec::then([this] { ++_follower._cleanups; })));
		}

		/// 唯一消费者（gate 的名单），所以不 `split`；不 `split` 就**必须私有**。
		[[nodiscard]] auto _follow_node(gate_job& hub) -> tg::node_sender
		{
			return tg::make_node(hub.work_node()
				// 这个 `continues_on` 不是可选的：名单是启动期才读的，读的时候 work
				// 早就完成了，订阅一个已完成的 `split` 会**原地同步**派发——所有
				// follower 会串在同一根线程上。换一次调度才真并行。
				| ::stdexec::continues_on(_context._scheduler)
				| ::stdexec::then([this, &hub]
					{
						if (_follower._late)
						{
							// 名单已经在 gate 启动时封存了，这里必然抛。
							hub.add_follower(hub.work_node());
						}

						if (_follower._fail)
						{
							throw ::std::runtime_error{_follower._name + ": 故意失败"};
						}

						++_follower._follows;
					}));
		}
	};

	gate& _gate;
	::std::string _name;
	bool _active = true;
	bool _fail = false;
	bool _late = false;
	::std::atomic<int> _follows{0};
	::std::atomic<int> _cleanups{0};

	follower(gate& target, ::std::string name)
		: _gate(target)
		, _name(::std::move(name))
	{}

	auto frame_job(context& target) -> void
	{
		target.add_job<job>(this, *this, target);
	}
};

// -------------------------------------------------------- 调用者自己的名单

/// entity 名单是**调用者的事**，库不管。两个指针，不分配。
struct entity_ref
{
	void* _object = nullptr;
	void (*_frame_job)(void*, context&) = nullptr;

	template <class entity_type>
	explicit entity_ref(entity_type& entity) noexcept
		: _object(&entity)
		, _frame_job([](void* object, context& target) { static_cast<entity_type*>(object)->frame_job(target); })
	{}

	auto frame_job(context& target) const -> void
	{
		_frame_job(_object, target);
	}
};

/// 一帧：造上下文 → 每个 entity 自己加入（阶段 A）→ `run_frame`（阶段 B + 跑）。
/// 上下文一析构，本帧全部 job 逆序销毁——帧隔离是结构性的。
auto run_one_frame(
	::std::vector<entity_ref> const& roster,
	::exec::static_thread_pool& pool,
	::std::uint64_t index,
	::stdexec::inplace_stop_token stop_token = {}) -> bool
{
	auto target = context{pool.get_scheduler(), index};

	for (auto&& entity : roster)
	{
		entity.frame_job(target);
	}

	return tg::run_frame(target, stop_token);
}

// ------------------------------------------------------------------- 场景

auto scenario_basic() -> void
{
	section("1. 基本：worker + gate + 两个 follower");

	auto pool = ::exec::static_thread_pool{4};
	auto tick = worker{};
	auto hub = gate{};
	auto first = follower{hub, "first"};
	auto second = follower{hub, "second"};

	auto roster = ::std::vector<entity_ref>{entity_ref{tick}, entity_ref{hub}, entity_ref{first}, entity_ref{second}};

	check(run_one_frame(roster, pool, 0), "整帧正常完成");
	check(tick._runs == 1, "纯任务 entity 跑了一次");
	check(hub._data._work_runs == 1, "共享的 work 只跑一次（两个 follower 都依赖它）");
	check(hub._data._done_runs == 1, "gate 收尾一次");
	check(first._follows == 1 && second._follows == 1, "两个 follower 各跑一次");
	check(first._cleanups == 1 && second._cleanups == 1, "两个 follower 各清理一次");
}

auto scenario_registration_order() -> void
{
	section("2. 注册顺序无关（gate 排最前 vs 排最后）");

	auto measure = [](bool gate_first)
	{
		auto pool = ::exec::static_thread_pool{4};
		auto hub = gate{};
		auto first = follower{hub, "first"};
		auto second = follower{hub, "second"};

		auto roster = gate_first
			? ::std::vector<entity_ref>{entity_ref{hub}, entity_ref{first}, entity_ref{second}}
			: ::std::vector<entity_ref>{entity_ref{first}, entity_ref{second}, entity_ref{hub}};

		auto const completed = run_one_frame(roster, pool, 0);
		return ::std::tuple{completed, hub._data._done_runs.load(), first._follows + second._follows};
	};

	auto const [ok_first, done_first, follows_first] = measure(true);
	auto const [ok_last, done_last, follows_last] = measure(false);

	check(ok_first && ok_last, "两种顺序都正常完成");
	check(done_first == 1 && done_last == 1, "两种顺序 gate 都收尾一次");
	check(follows_first == 2 && follows_last == 2, "两种顺序 follower 都各跑一次");
}

auto scenario_conditional_and_empty() -> void
{
	section("3. 条件性参与 / 空名单");

	auto pool = ::exec::static_thread_pool{4};
	auto hub = gate{};
	auto idle = follower{hub, "idle"};
	idle._active = false;

	auto roster = ::std::vector<entity_ref>{entity_ref{hub}, entity_ref{idle}};

	check(run_one_frame(roster, pool, 0), "整帧正常完成");
	check(idle._follows == 0, "退出本帧的 follower 一条都没挂");
	check(hub._data._done_runs == 1, "空名单下 gate 照常收尾");
	check(idle._cleanups == 1, "它的清理仍然执行（清理挂在帧根，跟参不参与无关）");

	auto lonely = gate{};
	auto only = ::std::vector<entity_ref>{entity_ref{lonely}};
	check(run_one_frame(only, pool, 0), "一个 follower 都没有时整帧正常完成");
	check(lonely._data._done_runs == 1, "`dynamic_when_all({})` 立即完成，收尾照跑");
}

auto scenario_failure() -> void
{
	section("4. 错误传播（一个 follower 抛异常）");

	auto pool = ::exec::static_thread_pool{4};
	auto hub = gate{};
	auto good = follower{hub, "good"};
	auto bad = follower{hub, "bad"};
	bad._fail = true;

	auto roster = ::std::vector<entity_ref>{entity_ref{hub}, entity_ref{good}, entity_ref{bad}};

	auto message = ::std::string{};
	try
	{
		static_cast<void>(run_one_frame(roster, pool, 0));
	}
	catch (::std::exception const& error)
	{
		message = error.what();
	}

	check(message == "bad: 故意失败", ::std::string{"异常传到调用者："} + message);
	check(hub._data._done_runs == 1, "gate 的收尾仍然执行（错误路径）");
	check(good._follows == 1, "不广播取消，所以兄弟照样跑完");
	check(good._cleanups == 0 && bad._cleanups == 0, "下游清理没有在失败时被误跑");
}

auto scenario_cancel() -> void
{
	section("5. 整帧取消（上层 request_stop）");

	auto pool = ::exec::static_thread_pool{4};
	auto tick = worker{};
	auto hub = gate{};
	auto one = follower{hub, "one"};

	auto roster = ::std::vector<entity_ref>{entity_ref{tick}, entity_ref{hub}, entity_ref{one}};

	auto source = ::stdexec::inplace_stop_source{};
	source.request_stop();

	check(!run_one_frame(roster, pool, 0, source.get_token()), "整帧报告为已取消");
	check(hub._data._work_runs == 0, "干活的节点没跑（取消从 `write_env` 那条路咬住）");
	check(one._follows == 0, "follower 没跑");
	check(hub._data._done_runs == 1, "**收尾仍然执行**（取消路径）");
	check(tick._runs == 0, "纯任务也被取消了");
}

auto scenario_late_registration() -> void
{
	section("6. 迟到注册（名单封存后再挂）");

	auto pool = ::exec::static_thread_pool{4};
	auto hub = gate{};
	auto sneaky = follower{hub, "sneaky"};
	sneaky._late = true;

	auto roster = ::std::vector<entity_ref>{entity_ref{hub}, entity_ref{sneaky}};

	auto message = ::std::string{};
	try
	{
		static_cast<void>(run_one_frame(roster, pool, 0));
	}
	catch (::std::exception const& error)
	{
		message = error.what();
	}

	check(message == "gate: 名单已封存，注册来晚了", ::std::string{"迟到注册是响亮的异常："} + message);
	check(hub._data._done_runs == 1, "收尾仍然执行");
}

auto scenario_dynamic_roster() -> void
{
	section("7. 动态名单 + 帧间隔离（连续 3 帧，中途增删）");

	auto pool = ::exec::static_thread_pool{4};
	auto hub = gate{};
	auto steady = follower{hub, "steady"};
	auto guest = follower{hub, "guest"};

	auto ok = true;
	for (auto index = ::std::uint64_t{0}; index != 3; ++index)
	{
		auto roster = ::std::vector<entity_ref>{entity_ref{hub}, entity_ref{steady}};
		if (index == 1)
		{
			roster.emplace_back(guest);
		}

		ok = run_one_frame(roster, pool, index) && ok;
	}

	check(ok, "三帧都正常完成");
	check(hub._data._done_runs == 3, "每帧恰好收尾一次");
	check(hub._data._work_runs == 3, "每帧一个全新的 job / 全新的 split");
	check(steady._follows == 3, "常驻 follower 每帧都跑");
	check(guest._follows == 1, "临时 follower 只在它在场的那帧跑");

	auto orphan = follower{hub, "orphan"};
	auto without_gate = ::std::vector<entity_ref>{entity_ref{orphan}};
	auto message = ::std::string{};
	try
	{
		static_cast<void>(run_one_frame(without_gate, pool, 9));
	}
	catch (::std::exception const& error)
	{
		message = error.what();
	}

	check(message == "task graph: 依赖的 entity 未参与本帧", ::std::string{"缺席的依赖被当场点名："} + message);
}

auto scenario_many_jobs() -> void
{
	section("8. 一个 entity 挂多个 job（job 不归 entity 持有）");

	struct multi
	{
		struct counter
		{
			::std::atomic<int>& _target;
			context& _context;

			auto build() -> void
			{
				_context.add(tg::make_node(
					::stdexec::starts_on(_context._scheduler, ::stdexec::just())
						| ::stdexec::then([this] { ++_target; })));
			}
		};

		::std::atomic<int> _runs{0};

		auto frame_job(context& target) -> void
		{
			// 三个 job，全归上下文托管。entity 手里一份都没有。
			target.add_job<counter>(this, _runs, target);
			target.add_job<counter>(this, _runs, target);
			target.add_job<counter>(this, _runs, target);
		}
	};

	auto pool = ::exec::static_thread_pool{4};
	auto many = multi{};
	auto roster = ::std::vector<entity_ref>{entity_ref{many}};

	check(run_one_frame(roster, pool, 0), "整帧正常完成");
	check(many._runs == 3, "三个 job 各挂一条根节点，都跑了");
}

}

auto main() -> int
{
	::std::println("task-graph 原语冒烟测试");

	scenario_basic();
	scenario_registration_order();
	scenario_conditional_and_empty();
	scenario_failure();
	scenario_cancel();
	scenario_late_registration();
	scenario_dynamic_roster();
	scenario_many_jobs();

	::std::println("");
	if (g_failures == 0)
	{
		::std::println("全部通过。");
		return 0;
	}

	::std::println("{} 项失败。", g_failures);
	return 1;
}
