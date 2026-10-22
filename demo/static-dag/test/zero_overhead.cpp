/// 0 开销验证：
///   1. 融合策略一次连接 + 启动的堆分配次数为 0；组合策略（split）不为 0。
///   2. 整图 op-state 的大小 = 节点 op-state 之和 + 值槽 + 必要的原子量，没有多余的间接层。
///   3. 不会失败的图没有停止源、没有状态字；只有一个汇点的图没有汇点计数；入度 1 的节点没有倒计数。

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <print>
#include <tuple>
#include <utility>

#include <stdexec/execution.hpp>

#include <sdag/sdag.h>

#include "check.h"

namespace
{

::std::atomic<::std::size_t> allocation_count{0};

struct a
{
};
struct b
{
};
struct c
{
};
struct d
{
};

template<class Policy>
auto diamond(Policy policy)
{
	return ::sdag::dag(policy,
		::sdag::make_node<a>(::stdexec::just(1)),
		::sdag::make_node<b, ::sdag::after<a>>(::stdexec::then([](int const& x) noexcept { return x + 1; })),
		::sdag::make_node<c, ::sdag::after<a>>(::stdexec::then([](int const& x) noexcept { return x * 2; })),
		::sdag::make_node<d, ::sdag::after<b, c>>(::stdexec::then([](int y, int z) noexcept { return y + z; })));
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
template<class Policy>
::std::size_t allocations_of_one_run(Policy policy, int& sink)
{
	auto const before = allocation_count.load();
	auto operation = ::stdexec::connect(diamond(policy), null_receiver{&sink});
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
		auto fused_allocations = allocations_of_one_run(::sdag::fused{}, sink);
		test::check(sink == 4 && fused_allocations == 0, "融合策略：构造 + 连接 + 启动 0 次分配");
		::std::println("  fused:    {} 次分配", fused_allocations);

		auto composed_allocations = allocations_of_one_run(::sdag::composed{}, sink);
		test::check(sink == 4 && composed_allocations > 0, "组合策略：split 在构造时就要分配共享状态");
		::std::println("  composed: {} 次分配（split）", composed_allocations);
	}

	test::section("op-state 大小");
	{
		using graph = decltype(diamond(::sdag::fused{}));
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
			::sdag::make_node<b, ::sdag::after<a>>(::stdexec::then([](int x) { return x + 1; }))));
		using throwing_operation = ::stdexec::connect_result_t<throwing_graph, null_receiver>;
		static_assert(throwing_operation::stoppable, "可能抛异常的节点需要停止源");
		::std::println("  可失败的两节点链 sizeof(op-state) = {}（含 inplace_stop_source、状态字、exception_ptr 槽）", sizeof(throwing_operation));
	}

	return test::report();
}
