/// 0 开销验证：
///   1. 一次构造 + 连接 + 启动的堆分配次数为 0。
///   2. 整图 op-state 的大小 = 节点 op-state 之和 + 值槽 + 必要的原子量，没有多余的间接层。
///   3. 不会失败的图没有停止源、没有状态字；只有一个汇点的图没有汇点计数；入度 1 的节点没有倒计数。

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <optional>
#include <print>
#include <tuple>
#include <utility>

#include <stdexec/execution.hpp>

#include <sdag/sdag.h>

#include "check.h"

namespace
{

::std::atomic<::std::size_t> allocation_count{0};

enum class key
{
	a, b, c, d, e,
};
inline constexpr key a = key::a;
inline constexpr key b = key::b;
inline constexpr key c = key::c;
inline constexpr key d = key::d;
inline constexpr key e = key::e;

auto diamond()
{
	return ::sdag::dag(
		::sdag::make_node<a>(::stdexec::just(1)),
		::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([](int const& x) noexcept { return x + 1; })),
		::sdag::make_node<c, ::sdag::data<a>>(::stdexec::then([](int const& x) noexcept { return x * 2; })),
		::sdag::make_node<d, ::sdag::data<::sdag::take{b}, ::sdag::take{c}>>(::stdexec::then([](int y, int z) noexcept { return y + z; })));
}

struct null_receiver
{
	using receiver_concept = ::stdexec::receiver_t;

	int* _sink;

	void set_value(int value) noexcept
	{
		*_sink = value;
	}

	template<class Error>
	void set_error(Error&&) noexcept
	{
	}

	void set_stopped() noexcept
	{
	}

	[[nodiscard]] ::stdexec::env<> get_env() const noexcept
	{
		return {};
	}
};

/// 从构造 sender 到启动完成（内联调度，整图同步跑完）的堆分配次数。
::std::size_t allocations_of_one_run(int& sink)
{
	auto const before = allocation_count.load();
	auto operation = ::stdexec::connect(diamond(), null_receiver{&sink});
	::stdexec::start(operation);
	return allocation_count.load() - before;
}

}

void* operator new(::std::size_t size)
{
	allocation_count.fetch_add(1, ::std::memory_order_relaxed);
	if (void* pointer = ::std::malloc(size != 0 ? size : 1))
	{
		return pointer;
	}
	throw ::std::bad_alloc{};
}

void operator delete(void* pointer) noexcept
{
	::std::free(pointer);
}

void operator delete(void* pointer, ::std::size_t) noexcept
{
	::std::free(pointer);
}

int main()
{
	test::section("堆分配");
	{
		int sink = 0;
		auto allocations = allocations_of_one_run(sink);
		test::check(sink == 4 && allocations == 0, "构造 + 连接 + 启动 0 次分配");
		::std::println("  {} 次分配", allocations);
	}

	test::section("op-state 大小");
	{
		using graph = decltype(diamond());
		using operation = ::stdexec::connect_result_t<graph, null_receiver>;
		static_assert(!operation::stoppable, "全 noexcept 的菱形不需要停止源");
		static_assert(operation::sink_count == 1);

		// 节点 op-state：just<int> -> 4 字节值 + 接收者指针；then -> 接收者指针 + 空输入 op；
		// 值槽：a 被两个后继共享 -> optional<tuple<int>>；b、c 各给 d -> optional<tuple<int>>；
		// d 是唯一的汇点，值直接转发给接收者，不存槽位；d 入度 2 -> 一个 atomic<size_t>
		constexpr auto node_operations = sizeof(operation::operations_t);
		constexpr auto receiver = sizeof(null_receiver);
		constexpr auto slots = 3 * sizeof(::std::optional<::std::tuple<int>>);
		constexpr auto pending = sizeof(::std::atomic<::std::size_t>);
		::std::println("  sizeof(op-state) = {}，节点 op-state {} + 接收者 {} + 3 个值槽 {} + 入度计数 {} = {}",
			sizeof(operation), node_operations, receiver, slots, pending, node_operations + receiver + slots + pending);
		test::check(sizeof(operation) == node_operations + receiver + slots + pending,
			"没有停止源 / 状态字 / 汇点计数等多余成员，被优化掉的成员不占字节");

		using throwing_graph = decltype(::sdag::dag(
			::sdag::make_node<a>(::stdexec::just(1)),
			::sdag::make_node<b, ::sdag::data<::sdag::take{a}>>(::stdexec::then([](int x) { return x + 1; }))));
		using throwing_operation = ::stdexec::connect_result_t<throwing_graph, null_receiver>;
		static_assert(throwing_operation::stoppable, "可能抛异常的节点需要停止源");
		::std::println("  可失败的两节点链 sizeof(op-state) = {}（含 inplace_stop_source、状态字、exception_ptr 槽）", sizeof(throwing_operation));
	}

	test::section("值槽复用：take 了的值析构之后，空间给后面的值用");
	{
		// 链 a -> b -> c -> d -> e，每一步 take 前驱。b 运行时 a 的值还在（b 拿的是指向槽位的右值，直到 b 结束才析构），
		// b 的值也已产生：任一时刻至多两个中间值在世，4 个中间值共用两块（a、c 一块，b、d 一块）。
		using chain = decltype(::sdag::dag(
			::sdag::make_node<a>(::stdexec::just(::std::array<int, 16>{})),
			::sdag::make_node<b, ::sdag::data<::sdag::take{a}>>(::stdexec::then([](::std::array<int, 16> v) noexcept { return v; })),
			::sdag::make_node<c, ::sdag::data<::sdag::take{b}>>(::stdexec::then([](::std::array<int, 16> v) noexcept { return v; })),
			::sdag::make_node<d, ::sdag::data<::sdag::take{c}>>(::stdexec::then([](::std::array<int, 16> v) noexcept { return v; })),
			::sdag::make_node<e, ::sdag::data<::sdag::take{d}>>(::stdexec::then([](::std::array<int, 16> v) noexcept { return v[0]; }))));
		using chain_operation = ::stdexec::connect_result_t<chain, null_receiver>;
		constexpr auto chain_slots = sizeof(chain_operation::state_base::_slots);
		::std::println("  take 链（4 个 64 字节的中间值）：值槽 {} 字节", chain_slots);
		test::check(chain_slots == 2 * sizeof(::std::array<int, 16>), "4 个中间值共用两块 64 字节的存储（平凡析构，没有在世标记）");

		// 不 take：每个中间值活到整图结束，各占一块。
		using kept = decltype(::sdag::dag(
			::sdag::make_node<a>(::stdexec::just(::std::array<int, 16>{})),
			::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([](::std::array<int, 16> const& v) noexcept { return v; })),
			::sdag::make_node<c, ::sdag::data<b>>(::stdexec::then([](::std::array<int, 16> const& v) noexcept { return v; })),
			::sdag::make_node<d, ::sdag::data<c>>(::stdexec::then([](::std::array<int, 16> const& v) noexcept { return v; })),
			::sdag::make_node<e, ::sdag::data<d>>(::stdexec::then([](::std::array<int, 16> const& v) noexcept { return v[0]; }))));
		using kept_operation = ::stdexec::connect_result_t<kept, null_receiver>;
		constexpr auto kept_slots = sizeof(kept_operation::state_base::_slots);
		::std::println("  不 take 的同一条链：值槽 {} 字节", kept_slots);
		test::check(kept_slots == 4 * sizeof(::std::optional<::std::array<int, 16>>), "不 take 时每个值各占一块（optional）");
	}

	return test::report();
}
