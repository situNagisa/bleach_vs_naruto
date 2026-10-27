/// sdag-compose 测试：降解后的图与融合路径（::sdag::dag）结果一致；扇入扇出可配置；控制依赖的降解。

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include <sdag/sdag.h>
#include <sdag/compose/compose.h>

#include "check.h"
#include "fixtures.h"

namespace
{

using ::sdag::compose::composed;

template<class Entry>
auto diamond(Entry entry, bool& b_shared, bool& c_shared, bool& d_unique)
{
	return entry(
		::sdag::make<::sdag::basic_node{a}>(::stdexec::just(1)),
		::sdag::make<::sdag::basic_node{b, ::sdag::let(a)}>(::stdexec::then([&](auto&& x) { b_shared = shared_access<decltype(x)>; return x + 1; })),
		::sdag::make<::sdag::basic_node{c, ::sdag::let(a)}>(::stdexec::then([&](auto&& x) { c_shared = shared_access<decltype(x)>; return x * 2; })),
		::sdag::make<::sdag::basic_node{d, ::sdag::let(b), ::sdag::let(c)}>(::stdexec::then([&](auto&& y, auto&& z)
		{
			d_unique = !shared_access<decltype(y)> && !shared_access<decltype(z)>;
			return y + z;
		})));
}

void test_diamond()
{
	test::section("菱形：降解后扇出经 split 按 const& 共享，结果与融合一致");
	bool b_shared = false;
	bool c_shared = false;
	bool d_unique = false;
	auto [result] = ::stdexec::sync_wait(diamond(composed<key>{}, b_shared, c_shared, d_unique)).value();
	test::check(result == 4, "结果 = (1+1) + (1*2)");
	test::check(b_shared && c_shared, "a 有两个后继，经 split 按 const& 共享");
	test::check(d_unique, "b、c 各只有一个后继，按值移动");
}

void test_triangle()
{
	test::section("三角形 a -> b -> c, a -> c");
	::std::vector<int> sequence;
	bool a_shared_in_c = true;
	auto g = composed<key>{}(
		::sdag::make<::sdag::basic_node{a}>(::stdexec::just(::std::string("a")) | ::stdexec::then([&](::std::string s) { sequence.push_back(1); return s; })),
		::sdag::make<::sdag::basic_node{b, ::sdag::let(a)}>(::stdexec::then([&](::std::string const& s) { sequence.push_back(2); return s + "b"; })),
		::sdag::make<::sdag::basic_node{c, ::sdag::let(a), ::sdag::let(b)}>(::stdexec::then([&](auto&& from_a, auto&& from_b)
		{
			sequence.push_back(3);
			a_shared_in_c = shared_access<decltype(from_a)>;
			return from_a + from_b;
		})));
	auto [text] = ::stdexec::sync_wait(::std::move(g)).value();
	test::check(text == "aab" && sequence == ::std::vector<int>{1, 2, 3}, "c 拿到 a、b 的值，且只启动一次");
	// when_all 会把 split 给的 const& 再拷贝一份存进自己的 op-state，然后按右值交给后继
	test::check(!a_shared_in_c, "when_all 把共享值拷贝后按右值交给后继（与融合路径不同）");
}

template<class Entry>
auto mixed(Entry entry, ::std::vector<int>& sequence)
{
	return entry(
		::sdag::make<::sdag::basic_node{a}>(::stdexec::just(5) | ::stdexec::then([&](int x) { sequence.push_back(1); return x; })),
		::sdag::make<::sdag::basic_node{b, ::sdag::let(a)}>(::stdexec::then([&](int x) { sequence.push_back(2); return x * 2; })),
		::sdag::make<::sdag::basic_node{c, ::sdag::wait(a)}>(::stdexec::just(::std::string("c")) | ::stdexec::then([&](::std::string s) { sequence.push_back(3); return s; })),
		::sdag::make<::sdag::basic_node{d, ::sdag::let(c), ::sdag::wait(b)}>(::stdexec::then([&](::std::string const& s) { sequence.push_back(4); return s + "d"; })));
}

void test_mixed_dependencies()
{
	test::section("data + control：数据前驱进 fanin，控制前驱丢掉值后并进 fanin");
	auto respects_order = [](::std::vector<int> const& sequence)
	{
		auto position = [&](int node) { return ::std::ranges::find(sequence, node) - sequence.begin(); };
		return sequence.size() == 4 && position(1) < position(2) && position(1) < position(3) && position(2) < position(4) && position(3) < position(4);
	};
	::std::vector<int> fused_sequence;
	::std::vector<int> composed_sequence;
	auto [fused_value] = ::stdexec::sync_wait(mixed(::sdag::dag<key>, fused_sequence)).value();
	auto [composed_value] = ::stdexec::sync_wait(mixed(composed<key>{}, composed_sequence)).value();
	test::check(fused_value == "cd" && composed_value == "cd", "两条路径都只把 c 的值交给 d");
	test::check(respects_order(fused_sequence) && respects_order(composed_sequence), "两条路径都遵守先后");
}

struct side
{
	bool right;
	::std::size_t index;

	friend constexpr bool operator==(side, side) noexcept = default;
};

constexpr side left(::std::size_t index) noexcept
{
	return {false, index};
}

constexpr side right(::std::size_t index) noexcept
{
	return {true, index};
}

template<::std::size_t... I>
auto make_ladder(::std::index_sequence<I...>, auto entry)
{
	return entry(
		::sdag::make<::sdag::basic_node{left(0)}>(::stdexec::just(1)),
		::sdag::make<::sdag::basic_node{right(0)}>(::stdexec::just(2)),
		::sdag::make<::sdag::basic_node{left(I + 1), ::sdag::let(left(I)), ::sdag::let(right(I))}>(::stdexec::then([](int const& x, int const& y) { return x + y; }))...,
		::sdag::make<::sdag::basic_node{right(I + 1), ::sdag::let(left(I)), ::sdag::let(right(I))}>(::stdexec::then([](int const& x, int const& y) { return x * 2 + y; }))...);
}

void test_shapes_agree()
{
	test::section("复杂形状：降解与融合结果一致");
	auto build = [](auto entry)
	{
		return entry(
			::sdag::make<::sdag::basic_node{a}>(::stdexec::just(2)),
			::sdag::make<::sdag::basic_node{b}>(::stdexec::just(3)),
			::sdag::make<::sdag::basic_node{c, ::sdag::let(a), ::sdag::let(b)}>(::stdexec::then([](int const& x, int const& y) { return x * y; })),
			::sdag::make<::sdag::basic_node{d, ::sdag::let(a)}>(::stdexec::then([](int const& x) { return x + 100; })),
			::sdag::make<::sdag::basic_node{e, ::sdag::let(c), ::sdag::let(b)}>(::stdexec::then([](int const& xy, int const& y) { return xy - y; })),
			::sdag::make<::sdag::basic_node{f, ::sdag::let(c), ::sdag::let(d), ::sdag::let(e)}>(::stdexec::then([](int const& xy, int dx, int diff) { return xy + dx + diff; })));
	};
	auto [fused_value] = ::stdexec::sync_wait(build(::sdag::dag<key>)).value();
	auto [composed_value] = ::stdexec::sync_wait(build(composed<key>{})).value();
	auto [custom_value] = ::stdexec::sync_wait(build(composed<key, ::stdexec::when_all_t, ::exec::split_t>{::stdexec::when_all, ::exec::split})).value();
	test::check(fused_value == 6 + 102 + 3 && composed_value == fused_value && custom_value == fused_value, "默认与显式传入 when_all / split 都与融合一致");

	// 8 层菱形梯子（路径数指数级）：降解的类型计算也应线性
	auto [fused_left, fused_right] = ::stdexec::sync_wait(make_ladder(::std::make_index_sequence<8>{}, ::sdag::dag<side>)).value();
	auto [composed_left, composed_right] = ::stdexec::sync_wait(make_ladder(::std::make_index_sequence<8>{}, composed<side>{})).value();
	test::check(fused_left == composed_left && fused_right == composed_right, "梯子：降解与融合一致");

	// 节点里有 7 个子 sender 的 when_all：拆开 / 重建 sender 表达式不受 stdexec __tuple 结构化绑定上限影响
	auto seven = [](auto entry)
	{
		auto ones = [] { return ::stdexec::just(1); };
		return entry(
			::sdag::make<::sdag::basic_node{a}>(::stdexec::when_all(ones(), ones(), ones(), ones(), ones(), ones(), ones())
				| ::stdexec::then([](auto... xs) { return (xs + ...); })),
			::sdag::make<::sdag::basic_node{b, ::sdag::let(a)}>([ones](auto input)
			{
				return ::stdexec::when_all(::std::move(input), ones(), ones(), ones(), ones(), ones(), ones())
					| ::stdexec::then([](auto... xs) { return (xs + ...); });
			}));
	};
	auto [wide] = ::stdexec::sync_wait(seven(composed<key>{})).value();
	test::check(wide == 13, "宽 when_all 节点降解后结果一致");
}

void test_lower()
{
	test::section("lower(graph, fanin, fanout)：手动降解一张 ::sdag::dag 建的图");
	auto g = ::sdag::dag<key>(
		::sdag::make<::sdag::basic_node{a}>(::stdexec::just(1)),
		::sdag::make<::sdag::basic_node{b, ::sdag::let(a)}>(::stdexec::then([](int const& x) { return x + 1; })),
		::sdag::make<::sdag::basic_node{c, ::sdag::let(a)}>(::stdexec::then([](int const& x) { return x * 2; })),
		::sdag::make<::sdag::basic_node{d, ::sdag::let(b), ::sdag::let(c)}>(::stdexec::then([](int y, int z) { return y + z; })),
		::sdag::make<::sdag::basic_node{e, ::sdag::let(c)}>(::stdexec::then([](int z) { return z * 10; })));
	auto [x, y] = ::stdexec::sync_wait(::sdag::compose::lower(::std::move(g), ::stdexec::when_all, ::exec::split)).value();
	test::check(x == 4 && y == 20, "两个汇点 d、e 的值按声明顺序");
}

}

int main()
{
	test_diamond();
	test_triangle();
	test_mixed_dependencies();
	test_shapes_agree();
	test_lower();
	return test::report();
}
