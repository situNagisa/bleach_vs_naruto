/// sdag 功能测试：编译期（concept、形状分析、完成签名）与运行期（各种形状、错误 / 停止传播、线程池）。

#include <algorithm>
#include <atomic>
#include <concepts>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <sdag/sdag.h>

#include "check.h"
#include "fixtures.h"

namespace
{

// ---------------------------------------------------------------- 编译期

namespace compile_time
{

using root = ::sdag::node<a, ::sdag::dependencies<>, decltype(::stdexec::just(1))>;
using inc = ::sdag::node<b, ::sdag::dependencies_of_t<::sdag::data<a>>, decltype(::stdexec::then([](int) { return 1.0; }))>;
using dag_type = ::sdag::dag_sender<root, inc>;

static_assert(::stdexec::sender<dag_type>);
static_assert(::stdexec::sender_in<dag_type, ::stdexec::env<>>);
static_assert(::std::same_as<::stdexec::completion_signatures_of_t<dag_type, ::stdexec::env<>>,
	::stdexec::completion_signatures<::stdexec::set_value_t(double), ::stdexec::set_error_t(::std::exception_ptr)>>);

// 用户写法里的 pack<sndr, fanin<...>> 对应 node<key, dependencies<data<...>, control<...>>, payload>
static_assert(::sdag::graph_node<root>);
static_assert(::sdag::graph_node<inc const&>);
static_assert(!::sdag::graph_node<int>);
static_assert(!::sdag::graph_node<::sdag::data<a>>);
static_assert(::sdag::dependency_list<::sdag::dependencies<>>);
static_assert(!::sdag::dependency_list<::sdag::data<a>>);
static_assert(::std::same_as<::sdag::dependencies_of_t<::sdag::control<b>>, ::sdag::dependencies<::sdag::data<>, ::sdag::control<b>>>);
static_assert(!::sdag::dependency_list<::std::tuple<>>);

// 形状诊断
using n_a = ::sdag::node<a, ::sdag::dependencies<>, int>;
using n_b = ::sdag::node<b, ::sdag::dependencies_of_t<::sdag::data<a>>, int>;
using n_c = ::sdag::node<c, ::sdag::dependencies_of_t<::sdag::data<a, b>>, int>;
static_assert(::sdag::shape_of<n_a, n_b, n_c>().error == ::sdag::general::shape_error::none);
static_assert(::sdag::shape_of<n_a, n_a>().error == ::sdag::general::shape_error::duplicate_key);
static_assert(::sdag::shape_of<n_b>().error == ::sdag::general::shape_error::unknown_dependency);
static_assert(::sdag::shape_of<::sdag::node<a, ::sdag::dependencies_of_t<::sdag::data<a>>, int>>().error == ::sdag::general::shape_error::self_dependency);
static_assert(::sdag::shape_of<n_a, ::sdag::node<b, ::sdag::dependencies_of_t<::sdag::data<a, a>>, int>>().error == ::sdag::general::shape_error::duplicate_dependency);
static_assert(::sdag::shape_of<::sdag::node<a, ::sdag::dependencies_of_t<::sdag::data<b>>, int>, ::sdag::node<b, ::sdag::dependencies_of_t<::sdag::data<a>>, int>>().error == ::sdag::general::shape_error::cycle);

// 拓扑序：声明顺序 c, a, b（c 依赖 a、b）-> 顺序 a, b, c
constexpr auto shape = ::sdag::shape_of<n_c, n_a, n_b>();
static_assert(shape.order[0] == 1 && shape.order[1] == 2 && shape.order[2] == 0);
static_assert(shape.in_degree[0] == 2 && shape.out_degree[1] == 2);
static_assert(shape.root_count() == 1 && shape.sink_count() == 1);

// 不会失败的图没有停止源；会失败的有
using nothrow_root = ::sdag::node<a, ::sdag::dependencies<>, decltype(::stdexec::just(1))>;
using nothrow_inc = ::sdag::node<b, ::sdag::dependencies_of_t<::sdag::data<a>>, decltype(::stdexec::then([](int x) noexcept { return x + 1; }))>;
using throwing_inc = ::sdag::node<b, ::sdag::dependencies_of_t<::sdag::data<a>>, decltype(::stdexec::then([](int x) { return x + 1; }))>;
using nothrow_op = ::stdexec::connect_result_t<::sdag::dag_sender<nothrow_root, nothrow_inc>, probe_receiver>;
using throwing_op = ::stdexec::connect_result_t<::sdag::dag_sender<nothrow_root, throwing_inc>, probe_receiver>;
static_assert(!nothrow_op::stoppable);
static_assert(throwing_op::stoppable);
static_assert(::std::same_as<::stdexec::completion_signatures_of_t<::sdag::dag_sender<nothrow_root, nothrow_inc>, ::stdexec::env<>>,
	::stdexec::completion_signatures<::stdexec::set_value_t(int)>>);

// 依赖环境的节点：无环境时算不出签名（sender 仍成立），给环境就能算
using env_dependent = ::sdag::node<a, ::sdag::dependencies<>,
	decltype(::stdexec::read_env(::stdexec::get_stop_token) | ::stdexec::then([](auto) { return 1; }))>;
using env_dag = ::sdag::dag_sender<env_dependent>;
static_assert(::stdexec::sender<env_dag>);
static_assert(!::stdexec::sender_in<env_dag>);
static_assert(::stdexec::sender_in<env_dag, ::stdexec::env<>>);

// 节点签名在某个环境下算不出来时，结果原样透传给 stdexec：sender 成立、sender_in 不成立，不是硬错误
using needs_scheduler = ::sdag::node<a, ::sdag::dependencies<>,
	decltype(::stdexec::read_env(::stdexec::get_scheduler) | ::stdexec::then([](auto) { return 1; }))>;
using scheduler_dag = ::sdag::dag_sender<needs_scheduler>;
static_assert(::stdexec::sender<scheduler_dag>);
static_assert(!::stdexec::sender_in<scheduler_dag, ::stdexec::env<>>, "env<> 里没有调度器");
// 属性查询这条实例化路径同样不能硬错误：完成域退回默认 domain
static_assert(::std::same_as<decltype(::stdexec::get_completion_domain<::stdexec::set_value_t>(::stdexec::get_env(::std::declval<scheduler_dag&>()), ::stdexec::env<>{})),
	::stdexec::default_domain>);

// 空图：立即以 set_value() 完成
static_assert(::std::same_as<::stdexec::completion_signatures_of_t<::sdag::dag_sender<>, ::stdexec::env<>>,
	::stdexec::completion_signatures<::stdexec::set_value_t()>>);

}

// ---------------------------------------------------------------- 运行期

auto diamond(bool& b_shared, bool& c_shared, bool& d_unique)
{
	return ::sdag::dag(
		::sdag::make_node<a>(::stdexec::just(1)),
		::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([&](auto&& x) { b_shared = shared_access<decltype(x)>; return x + 1; })),
		::sdag::make_node<c, ::sdag::data<a>>(::stdexec::then([&](auto&& x) { c_shared = shared_access<decltype(x)>; return x * 2; })),
		::sdag::make_node<d, ::sdag::data<b, c>>(::stdexec::then([&](auto&& y, auto&& z)
		{
			d_unique = !shared_access<decltype(y)> && !shared_access<decltype(z)>;
			return y + z;
		})));
}

void test_diamond()
{
	test::section("菱形：扇出按 const& 共享，扇入按值独占");
	bool b_shared = false;
	bool c_shared = false;
	bool d_unique = false;
	int result = ::std::get<0>(::stdexec::sync_wait(diamond(b_shared, c_shared, d_unique)).value());
	test::check(result == 4, "结果 = (1+1) + (1*2)");
	test::check(b_shared && c_shared, "a 有两个后继，按 const& 共享");
	test::check(d_unique, "b、c 各只有一个后继，按值移动");
}

void test_triangle()
{
	test::section("三角形 a -> b -> c, a -> c：传递边不去重，a 共享、b 独占，c 等两个前驱都到齐");
	{
		::std::vector<int> sequence;
		bool a_shared_in_c = false;
		bool b_unique_in_c = false;
		auto build = [&]
		{
			return ::sdag::dag(
				::sdag::make_node<a>(::stdexec::just(::std::string("a")) | ::stdexec::then([&](::std::string s) { sequence.push_back(1); return s; })),
				::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([&](::std::string const& s) { sequence.push_back(2); return s + "b"; })),
				::sdag::make_node<c, ::sdag::data<a, b>>(::stdexec::then([&](auto&& from_a, auto&& from_b)
				{
					sequence.push_back(3);
					a_shared_in_c = shared_access<decltype(from_a)>;
					b_unique_in_c = !shared_access<decltype(from_b)>;
					return from_a + from_b;
				})));
		};
		using fused_graph = decltype(build());
		static_assert(::std::same_as<::stdexec::completion_signatures_of_t<fused_graph, ::stdexec::env<>>,
			::stdexec::completion_signatures<::stdexec::set_value_t(::std::string), ::stdexec::set_error_t(::std::exception_ptr)>>);
		constexpr auto shape = ::sdag::shape_v<fused_graph>;
		static_assert(shape.in_degree[2] == 2 && shape.out_degree[0] == 2 && shape.out_degree[1] == 1);
		static_assert(shape.order[0] == 0 && shape.order[1] == 1 && shape.order[2] == 2);

		::std::string text = ::std::get<0>(::stdexec::sync_wait(build()).value());
		test::check(text == "aab", "c 同时拿到 a 的值和 b 的值");
		test::check(sequence == ::std::vector<int>{1, 2, 3}, "c 只启动一次，且在 a、b 之后");
		test::check(a_shared_in_c && b_unique_in_c, "a 有两个后继按 const& 共享，b 只有 c 一个后继按值移动");
	}
}

void test_move_only_chain()
{
	test::section("链：move-only 值沿独占边移动；左值 sender 可重复连接");
	auto g = ::sdag::dag(
		::sdag::make_node<a>(::stdexec::just(::std::make_unique<int>(41))),
		::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([](::std::unique_ptr<int> p) { return ::std::make_unique<int>(*p + 1); })),
		::sdag::make_node<c, ::sdag::data<b>>(::stdexec::then([](::std::unique_ptr<int> p) { return *p; })));
	auto [value] = ::stdexec::sync_wait(::std::move(g)).value();
	test::check(value == 42, "链式传递");

	int connections = 0;
	auto reusable = ::sdag::dag(
		::sdag::make_node<a>(::stdexec::just(1)),
		::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([&](int x) { ++connections; return x + 1; })));
	auto [first] = ::stdexec::sync_wait(reusable).value();
	auto [second] = ::stdexec::sync_wait(reusable).value();
	test::check(first == 2 && second == 2 && connections == 2, "const& 连接两次，载荷被拷贝，各跑一次");
}

void test_multi_sink_and_control()
{
	test::section("多汇点按声明顺序拼接；控制依赖只定先后");
	::std::vector<int> sequence;
	auto g = ::sdag::dag(
		::sdag::make_node<a>(::stdexec::just(1)),
		::sdag::make_node<b, ::sdag::control<a>>(::stdexec::just(::std::string("ctrl")) | ::stdexec::then([&](::std::string s) { sequence.push_back(2); return s; })),
		::sdag::make_node<c, ::sdag::data<b>>(::stdexec::then([&](::std::string const&) { sequence.push_back(3); return 3.5; })),
		::sdag::make_node<d, ::sdag::data<a>>(::stdexec::then([&](int x) { sequence.push_back(4); return x * 10; })),
		::sdag::make_node<e, ::sdag::data<b>>(::stdexec::then([&](::std::string const& s) { sequence.push_back(5); return s.size(); })));
	static_assert(::std::same_as<::stdexec::completion_signatures_of_t<decltype(g), ::stdexec::env<>>,
		::stdexec::completion_signatures<::stdexec::set_value_t(double, int, ::std::size_t), ::stdexec::set_error_t(::std::exception_ptr)>>);
	auto [x, y, z] = ::stdexec::sync_wait(::std::move(g)).value();
	test::check(x == 3.5 && y == 10 && z == 4, "汇点 c, d, e 的值按声明顺序拼接");
	test::check(sequence.size() == 4 && sequence[0] == 2, "b 在 a 之后、在 c/e 之前跑");

	// 汇点没有值、根没有值
	auto bare = ::sdag::dag(
		::sdag::make_node<a>(::stdexec::just()),
		::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([] {})));
	static_assert(::std::same_as<::stdexec::completion_signatures_of_t<decltype(bare), ::stdexec::env<>>,
		::stdexec::completion_signatures<::stdexec::set_value_t(), ::stdexec::set_error_t(::std::exception_ptr)>>);
	test::check(::stdexec::sync_wait(::std::move(bare)).has_value(), "无值的图正常完成");

	test::check(::stdexec::sync_wait(::sdag::dag()).has_value(), "空图立即完成");
}

void test_mixed_dependencies()
{
	test::section("数据依赖 + 控制依赖：data<> 给值，control<> 只定先后");
	auto build = [](::std::vector<int>& sequence, bool& a_shared)
	{
		return ::sdag::dag(
			::sdag::make_node<a>(::stdexec::just(5) | ::stdexec::then([&](int x) { sequence.push_back(1); return x; })),
			// c 只依赖 a 的先后：a 的值只有 b 一个读者，所以 move 给 b，不共享
			::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([&](auto&& x) { a_shared = shared_access<decltype(x)>; sequence.push_back(2); return x * 2; })),
			::sdag::make_node<c, ::sdag::control<a>>(::stdexec::just(::std::string("c")) | ::stdexec::then([&](::std::string s) { sequence.push_back(3); return s; })),
			// d 等 b、c 都完成，但只收到 c 的值
			::sdag::make_node<d, ::sdag::data<c>, ::sdag::control<b>>(::stdexec::then([&](::std::string const& s) { sequence.push_back(4); return s + "d"; })));
	};
	using graph = decltype(build(::std::declval<::std::vector<int>&>(), ::std::declval<bool&>()));
	static_assert(::std::same_as<::stdexec::completion_signatures_of_t<graph, ::stdexec::env<>>,
		::stdexec::completion_signatures<::stdexec::set_value_t(::std::string), ::stdexec::set_error_t(::std::exception_ptr)>>);
	static_assert(::sdag::graph::reader_counts_v<graph&&>[0] == 1, "a 的值只有 b 读（c 是控制依赖）");
	static_assert(::sdag::graph::reader_counts_v<graph&&>[1] == 0, "b 的值没人读（d 对 b 是控制依赖）");

	auto respects_order = [](::std::vector<int> const& sequence)
	{
		auto position = [&](int node) { return ::std::ranges::find(sequence, node) - sequence.begin(); };
		return sequence.size() == 4 && position(1) < position(2) && position(1) < position(3) && position(2) < position(4) && position(3) < position(4);
	};
	::std::vector<int> fused_sequence;
	bool fused_shared = true;
	auto [fused_value] = ::stdexec::sync_wait(build(fused_sequence, fused_shared)).value();
	test::check(fused_value == "cd" && respects_order(fused_sequence) && !fused_shared, "d 只收到 c 的值，且在 b、c 之后跑；a 的值 move 给唯一读者 b");
}

void test_error_propagation()
{
	test::section("错误：未启动的后继跳过、在飞的兄弟收到停止请求、第一个错误胜出");
	{
		int ran_c = 0;
		int ran_d = 0;
		auto g = ::sdag::dag(
			::sdag::make_node<a>(::stdexec::just(1)),
			::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([](int) -> int { throw ::std::runtime_error("boom"); })),
			// 同一前驱按声明顺序通知后继：b 内联失败之后才轮到 c，此时 c 直接被跳过
			::sdag::make_node<c, ::sdag::data<a>>(::stdexec::then([&](int) { ++ran_c; return 2; })),
			::sdag::make_node<d, ::sdag::data<b, c>>(::stdexec::then([&](int, int) { ++ran_d; return 0; })));
		bool caught = false;
		try
		{
			::stdexec::sync_wait(::std::move(g));
		}
		catch (::std::runtime_error const& error)
		{
			caught = ::std::string_view{error.what()} == "boom";
		}
		test::check(caught, "sync_wait 重新抛出节点的异常");
		test::check(ran_c == 0 && ran_d == 0, "失败之后还没启动的节点全部跳过");
	}
	{
		::exec::static_thread_pool pool{2};
		auto scheduler = pool.get_scheduler();
		::std::atomic<bool> c_started{false};
		::std::atomic<bool> b_failed{false};
		bool sibling_saw_stop = false;
		int ran_d = 0;
		auto g = ::sdag::dag(
			::sdag::make_node<a>(::stdexec::just(1)),
			::sdag::make_node<b, ::sdag::data<a>>(::stdexec::continues_on(scheduler) | ::stdexec::then([&](int const&) -> int
			{
				c_started.wait(false);
				b_failed = true;
				b_failed.notify_all();
				throw ::std::runtime_error("boom");
			})),
			::sdag::make_node<c, ::sdag::control<a>>(::stdexec::read_env(::stdexec::get_stop_token) | ::stdexec::continues_on(scheduler)
				| ::stdexec::then([&](auto token)
				{
					c_started = true;
					c_started.notify_all();
					b_failed.wait(false);
					// b 的异常进入 set_error 后整图才请求停止，给它一点时间
					for (int spin = 0; spin < 200'000 && !token.stop_requested(); ++spin)
					{
						::std::this_thread::yield();
					}
					sibling_saw_stop = token.stop_requested();
					return 2;
				})),
			::sdag::make_node<d, ::sdag::data<b, c>>(::stdexec::then([&](int, int) { ++ran_d; return 0; })));
		bool caught = false;
		try
		{
			::stdexec::sync_wait(::std::move(g));
		}
		catch (::std::runtime_error const&)
		{
			caught = true;
		}
		test::check(caught, "在飞兄弟成功完成也不改变整图的错误结果");
		test::check(sibling_saw_stop, "在飞的兄弟节点看到停止请求");
		test::check(ran_d == 0, "失败节点的后继被跳过");
	}

	// 错误类型恰好是 std::monostate（与 variant 的占位类型同名）也要能存、能发
	{
		auto monostate_error = ::sdag::dag(::sdag::make_node<a>(::stdexec::just_error(::std::monostate{})));
		outcome result = outcome::none;
		int count = 0;
		auto op = ::stdexec::connect(::std::move(monostate_error), probe_receiver{&result, &count, {}});
		::stdexec::start(op);
		test::check(result == outcome::error, "std::monostate 作为错误类型");
	}

	// 两个根都失败：只保留第一个错误
	auto twice = ::sdag::dag(
		::sdag::make_node<a>(::stdexec::just_error(1)),
		::sdag::make_node<b>(::stdexec::just_error(::std::string("second"))));
	static_assert(::std::same_as<::stdexec::completion_signatures_of_t<decltype(twice), ::stdexec::env<>>,
		::stdexec::completion_signatures<::stdexec::set_error_t(int), ::stdexec::set_error_t(::std::string)>>);
	outcome result = outcome::none;
	int count = 0;
	auto op = ::stdexec::connect(::std::move(twice), probe_receiver{&result, &count, {}});
	::stdexec::start(op);
	test::check(result == outcome::error, "没有值完成的图以错误结束");

	// 存值时抛异常 -> exception_ptr（a 的值要存给 b 读；唯一汇点的值不存槽位，所以单节点图不会走这条路）
	bool explode = false;
	auto storing = ::sdag::dag(
		::sdag::make_node<a>(::stdexec::just(fragile{&explode})),
		::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([](fragile) noexcept { return 1; })));
	static_assert(::std::same_as<::stdexec::completion_signatures_of_t<decltype(storing), ::stdexec::env<>>,
		::stdexec::completion_signatures<::stdexec::set_value_t(int), ::stdexec::set_error_t(::std::exception_ptr)>>);
	using single = decltype(::sdag::dag(::sdag::make_node<a>(::stdexec::just(fragile{&explode}))));
	static_assert(::std::same_as<::stdexec::completion_signatures_of_t<single, ::stdexec::env<>>,
		::stdexec::completion_signatures<::stdexec::set_value_t(fragile)>>, "唯一汇点的值不存槽位，也就没有存值抛异常这回事");
	bool store_failed = false;
	explode = true;
	try
	{
		::stdexec::sync_wait(::std::move(storing));
	}
	catch (::std::runtime_error const& error)
	{
		store_failed = ::std::string_view{error.what()} == "fragile";
	}
	test::check(store_failed, "值移进 op-state 时抛出的异常变成 set_error(exception_ptr)");
}

void test_stop_propagation()
{
	test::section("停止：节点 stopped -> 后继跳过、整图 stopped；外层停止令牌按 when_all 语义处理");
	int ran_b = 0;
	auto g = ::sdag::dag(
		::sdag::make_node<a>(always_stopped{}),
		::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([&] { ++ran_b; return 1; })));
	test::check(!::stdexec::sync_wait(::std::move(g)).has_value() && ran_b == 0, "节点停止后整图停止，后继不跑");

	// 外层已请求停止 + 图能 stopped：整图 stopped，节点都不跑
	{
		::stdexec::inplace_stop_source source;
		source.request_stop();
		int ran = 0;
		auto stoppable = ::sdag::dag(
			::sdag::make_node<a>(stop_probe{}),
			::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([&] { ++ran; return 1; })));
		outcome result = outcome::none;
		int count = 0;
		auto op = ::stdexec::connect(::std::move(stoppable), probe_receiver{&result, &count, source.get_token()});
		::stdexec::start(op);
		test::check(result == outcome::stopped && ran == 0, "外层停止 + 可停止的图：以 stopped 完成");
	}
	// 外层已请求停止 + 图不能 stopped：照常以值完成（节点自己看到令牌）
	{
		::stdexec::inplace_stop_source source;
		source.request_stop();
		auto unstoppable = ::sdag::dag(
			::sdag::make_node<a>(::stdexec::just(1)),
			::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([](int x) { return x + 1; })),
			::sdag::make_node<c, ::sdag::data<a>>(::stdexec::then([](int x) { return x + 2; })));
		outcome result = outcome::none;
		int count = 0;
		auto op = ::stdexec::connect(::std::move(unstoppable), probe_receiver{&result, &count, source.get_token()});
		::stdexec::start(op);
		test::check(result == outcome::value && count == 2, "外层停止 + 不可停止的图：仍以两个汇点的值完成");
	}
	// 运行中从外层请求停止：线程池上的节点看到令牌后停止
	{
		::exec::static_thread_pool pool{2};
		::stdexec::inplace_stop_source source;
		::std::atomic<bool> started{false};
		auto g2 = ::sdag::dag(
			::sdag::make_node<a>(::stdexec::schedule(pool.get_scheduler()) | ::stdexec::then([&]
			{
				started = true;
				started.notify_all();
				while (!source.stop_requested())
				{
				}
			})),
			::sdag::make_node<b, ::sdag::control<a>>(stop_probe{}),
			::sdag::make_node<c, ::sdag::data<b>>(::stdexec::then([] { return 1; })));
		outcome result = outcome::none;
		int count = 0;
		::std::atomic<bool> done{false};
		auto op = ::stdexec::connect(::std::move(g2), notify_receiver{{&result, &count, source.get_token()}, &done});
		::stdexec::start(op);
		started.wait(false);
		source.request_stop();
		done.wait(false);
		test::check(result == outcome::stopped, "运行中外层请求停止：b 看到令牌后停止，整图 stopped");
	}
}

void test_thread_pool()
{
	test::section("线程池：宽扇出 / 扇入，多次执行无竞争");
	::exec::static_thread_pool pool{4};
	auto scheduler = pool.get_scheduler();
	for (int iteration = 0; iteration < 200; ++iteration)
	{
		::std::atomic<int> touched{0};
		auto on_pool = [&](auto fn) { return ::stdexec::continues_on(scheduler) | ::stdexec::then(fn); };
		auto g = ::sdag::dag(
			::sdag::make_node<a>(::stdexec::just(10)),
			::sdag::make_node<b, ::sdag::data<a>>(on_pool([&](int const& x) { ++touched; return x + 1; })),
			::sdag::make_node<c, ::sdag::data<a>>(on_pool([&](int const& x) { ++touched; return x + 2; })),
			::sdag::make_node<d, ::sdag::data<a>>(on_pool([&](int const& x) { ++touched; return x + 3; })),
			::sdag::make_node<e, ::sdag::data<b, c, d>>(on_pool([&](int x, int y, int z) { ++touched; return x + y + z; })),
			::sdag::make_node<f, ::sdag::data<b, d>>(on_pool([&](int const& x, int const& z) { ++touched; return x * z; })));
		auto [sum, product] = ::stdexec::sync_wait(::std::move(g)).value();
		if (sum != 36 || product != 143 || touched != 5)
		{
			test::check(false, "线程池上的结果错误");
			return;
		}
	}
	test::check(true, "200 次执行全部正确");
}

template<::std::size_t I>
struct step
{
};

template<::std::size_t I>
struct left
{
};

template<::std::size_t I>
struct right
{
};

/// 叠起来的菱形梯子：每一层的两个节点都依赖上一层的两个节点；节点刻意不按拓扑序声明。
template<::std::size_t... I>
auto make_ladder(::std::index_sequence<I...>)
{
	return ::sdag::dag(
		::sdag::make_node<left<0>>(::stdexec::just(1)),
		::sdag::make_node<right<0>>(::stdexec::just(2)),
		::sdag::make_node<left<I + 1>, ::sdag::data<left<I>, right<I>>>(::stdexec::then([](int const& x, int const& y) { return x + y; }))...,
		::sdag::make_node<right<I + 1>, ::sdag::data<left<I>, right<I>>>(::stdexec::then([](int const& x, int const& y) { return x * 2 + y; }))...);
}

template<::std::size_t... I>
auto make_chain(::std::index_sequence<I...>)
{
	return ::sdag::dag(
		::sdag::make_node<step<0>>(::stdexec::just(0)),
		::sdag::make_node<step<I + 1>, ::sdag::data<step<I>>>(::stdexec::then([](int x) { return x + 1; }))...);
}

template<::std::size_t... I>
auto make_fan(::std::index_sequence<I...>)
{
	return ::sdag::dag(
		::sdag::make_node<step<0>>(::stdexec::just(1)),
		::sdag::make_node<step<I + 1>, ::sdag::data<step<0>>>(::stdexec::then([](int const& x) { return x + static_cast<int>(I); }))...,
		::sdag::make_node<step<sizeof...(I) + 1>, ::sdag::data<step<I + 1>...>>(::stdexec::then([](auto... xs) { return (xs + ...); })));
}

void test_scale()
{
	test::section("规模：48 节点长链、1 -> 16 -> 1 扇出扇入");
	auto [depth] = ::stdexec::sync_wait(make_chain(::std::make_index_sequence<47>{})).value();
	test::check(depth == 47, "长链逐级 +1");
	auto [fan] = ::stdexec::sync_wait(make_fan(::std::make_index_sequence<16>{})).value();
	test::check(fan == 16 + (15 * 16) / 2, "扇入收齐 16 路");

	// 8 层菱形梯子（18 个节点，路径数指数级）：应线性编译
	auto [ladder_left, ladder_right] = ::stdexec::sync_wait(make_ladder(::std::make_index_sequence<8>{})).value();
	test::check(ladder_left == 1393 && ladder_right == 1970, "梯子：逐层 (x + y, 2x + y)");
}

void test_wide_expressions()
{
	test::section("节点里是子 sender 多于 6 个的 stdexec 算法（when_all 有 7 个子 sender）");
	auto seven = []
	{
		auto ones = [] { return ::stdexec::just(1); };
		return ::sdag::dag(
			::sdag::make_node<a>(::stdexec::when_all(ones(), ones(), ones(), ones(), ones(), ones(), ones())
				| ::stdexec::then([](auto... xs) { return (xs + ...); })),
			::sdag::make_node<b, ::sdag::data<a>>([ones](auto input)
			{
				return ::stdexec::when_all(::std::move(input), ones(), ones(), ones(), ones(), ones(), ones())
					| ::stdexec::then([](auto... xs) { return (xs + ...); });
			}));
	};
	auto [fused_value] = ::stdexec::sync_wait(seven()).value();
	test::check(fused_value == 13, "根节点 7 路 when_all，后继把输入与 6 个 sender 一起 when_all");
}

void test_ecosystem()
{
	test::section("stdexec 生态：图是 dag_t 标签的 sender；任何 domain 下都走融合 op-state");
	auto build = []
	{
		return ::sdag::dag(
			::sdag::make_node<a>(::stdexec::just(1)),
			::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([](int const& x) { return x + 1; })),
			::sdag::make_node<c, ::sdag::data<a>>(::stdexec::then([](int const& x) { return x * 2; })),
			::sdag::make_node<d, ::sdag::data<b, c>>(::stdexec::then([](int y, int z) { return y + z; })),
			::sdag::make_node<e, ::sdag::data<c>>(::stdexec::then([](int z) { return z * 10; })));
	};
	using graph = decltype(build());
	static_assert(::exec::sender_for<graph, ::sdag::dag_t>);
	static_assert(::std::same_as<::stdexec::tag_of_t<graph>, ::sdag::dag_t>);
	static_assert(::std::same_as<::std::tuple_element_t<2, graph>, decltype(::stdexec::just(1))>, "子 sender 就是用户给的节点 sender");

	// 没有 domain：连接结果是融合 op-state
	static_assert(::std::same_as<::stdexec::connect_result_t<graph, probe_receiver>,
		::sdag::execution::fused_operation<graph&&, probe_receiver>>);
	// 定制了 when_all 的 domain 也一样：dag_t 没有 transform_sender，图不会被改写
	static_assert(::std::same_as<::stdexec::connect_result_t<graph, domain_receiver>,
		::sdag::execution::fused_operation<graph&&, domain_receiver>>);

	custom_when_all_uses = 0;
	{
		outcome result = outcome::none;
		int count = 0;
		auto op = ::stdexec::connect(build(), probe_receiver{&result, &count, {}});
		::stdexec::start(op);
		test::check(result == outcome::value && count == 2 && custom_when_all_uses == 0, "默认 domain 下 when_all 定制从未被触发");
	}
	{
		outcome result = outcome::none;
		int count = 0;
		auto op = ::stdexec::connect(build(), domain_receiver{&result, &count});
		::stdexec::start(op);
		test::check(result == outcome::value && count == 2 && custom_when_all_uses == 0, "定制 when_all 的 domain 下仍走融合，定制不被触发");
	}

	// 完成域属性：图在汇点完成的地方完成
	{
		::exec::static_thread_pool pool{1};
		auto scheduler = pool.get_scheduler();
		auto on_pool = ::sdag::dag(
			::sdag::make_node<a>(::stdexec::just(1)),
			::sdag::make_node<b, ::sdag::data<a>>(::stdexec::continues_on(scheduler) | ::stdexec::then([](int x) { return x + 1; })));
		using env = ::stdexec::env<>;
		using graph_domain = decltype(::stdexec::get_completion_domain<::stdexec::set_value_t>(::stdexec::get_env(on_pool), env{}));
		using sink_domain = decltype(::stdexec::get_completion_domain<::stdexec::set_value_t>(
			::stdexec::get_env(::stdexec::continues_on(::stdexec::just(1), scheduler)), env{}));
		static_assert(::std::same_as<graph_domain, sink_domain>);
		static_assert(!::std::same_as<graph_domain, ::stdexec::default_domain>, "线程池有自己的 domain");
		// 线程池的 domain 不定制 when_all：仍然走融合
		static_assert(::std::same_as<::stdexec::connect_result_t<decltype(on_pool), probe_receiver>,
			::sdag::execution::fused_operation<decltype(on_pool)&&, probe_receiver>>);
		auto [value] = ::stdexec::sync_wait(::std::move(on_pool)).value();
		test::check(value == 2, "线程池上的图照常完成");
	}
}

}

int main()
{
	test_diamond();
	test_triangle();
	test_move_only_chain();
	test_multi_sink_and_control();
	test_mixed_dependencies();
	test_error_propagation();
	test_stop_propagation();
	test_thread_pool();
	test_scale();
	test_wide_expressions();
	test_ecosystem();
	return test::report();
}
