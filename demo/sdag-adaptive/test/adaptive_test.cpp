/// sdag-adaptive 测试：自适配图在定制了 when_all 的 domain 下降解、在其它 domain 下融合；::sdag::dag 建的图永远融合。

#include <concepts>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <sdag/sdag.h>
#include <sdag/adaptive/adaptive.h>

#include "check.h"
#include "fixtures.h"

namespace
{

auto build(auto entry)
{
	return entry(
		::sdag::make<::sdag::basic_node{a}>(::stdexec::just(1)),
		::sdag::make<::sdag::basic_node{b, ::sdag::let(a)}>(::stdexec::then([](int const& x) { return x + 1; })),
		::sdag::make<::sdag::basic_node{c, ::sdag::let(a)}>(::stdexec::then([](int const& x) { return x * 2; })),
		::sdag::make<::sdag::basic_node{d, ::sdag::let(b), ::sdag::let(c)}>(::stdexec::then([](int y, int z) { return y + z; })),
		::sdag::make<::sdag::basic_node{e, ::sdag::let(c)}>(::stdexec::then([](int z) { return z * 10; })));
}

void test_tag()
{
	test::section("标签：自适配图是 adaptive_dag_t 标签的图 sender");
	using graph = decltype(build(::sdag::adaptive::adaptive_dag<key>));
	static_assert(::exec::sender_for<graph, ::sdag::adaptive::adaptive_dag_t<key>>);
	static_assert(!::exec::sender_for<graph, ::sdag::dag_t<key>>);
	static_assert(::std::same_as<::stdexec::completion_signatures_of_t<graph, ::stdexec::env<>>,
		::stdexec::completion_signatures_of_t<decltype(build(::sdag::dag<key>)), ::stdexec::env<>>>, "完成签名与 ::sdag::dag 相同");
	auto [x, y] = ::stdexec::sync_wait(build(::sdag::adaptive::adaptive_dag<key>)).value();
	test::check(x == 4 && y == 20, "默认 domain 下照常完成");
}

void test_lowering_trigger()
{
	test::section("触发：定制了 when_all 的 domain 降解，默认 domain 与线程池 domain 融合");
	using graph = decltype(build(::sdag::adaptive::adaptive_dag<key>));
	static_assert(::std::same_as<::stdexec::connect_result_t<graph, probe_receiver>,
		::sdag::execution::fused_operation<graph&&, probe_receiver>>, "默认 domain：融合");
	static_assert(!::std::same_as<::stdexec::connect_result_t<graph, domain_receiver>,
		::sdag::execution::fused_operation<graph&&, domain_receiver>>, "mock_domain：降解成标准算法树");

	custom_when_all_uses = 0;
	{
		outcome result = outcome::none;
		int count = 0;
		auto op = ::stdexec::connect(build(::sdag::adaptive::adaptive_dag<key>), probe_receiver{&result, &count, {}});
		::stdexec::start(op);
		test::check(result == outcome::value && count == 2 && custom_when_all_uses == 0, "默认 domain 下 when_all 定制从未被触发");
	}
	{
		outcome result = outcome::none;
		int count = 0;
		auto op = ::stdexec::connect(build(::sdag::adaptive::adaptive_dag<key>), domain_receiver{&result, &count});
		::stdexec::start(op);
		test::check(result == outcome::value && count == 2, "降解后的图完成值与融合一致（两个汇点）");
		test::check(custom_when_all_uses > 0, "domain 对 when_all 的定制在降解后的图上生效");
	}
	{
		// 同一个 domain 下，::sdag::dag 建的图不降解
		custom_when_all_uses = 0;
		outcome result = outcome::none;
		int count = 0;
		auto op = ::stdexec::connect(build(::sdag::dag<key>), domain_receiver{&result, &count});
		::stdexec::start(op);
		test::check(result == outcome::value && custom_when_all_uses == 0, "::sdag::dag 建的图在 mock_domain 下仍融合");
	}

	// 线程池的 domain 不定制 when_all：仍然走融合
	::exec::static_thread_pool pool{1};
	auto scheduler = pool.get_scheduler();
	auto on_pool = ::sdag::adaptive::adaptive_dag<key>(
		::sdag::make<::sdag::basic_node{a}>(::stdexec::just(1)),
		::sdag::make<::sdag::basic_node{b, ::sdag::let(a)}>(::stdexec::continues_on(scheduler) | ::stdexec::then([](int x) { return x + 1; })));
	static_assert(::std::same_as<::stdexec::connect_result_t<decltype(on_pool), probe_receiver>,
		::sdag::execution::fused_operation<decltype(on_pool)&&, probe_receiver>>);
	auto [value] = ::stdexec::sync_wait(::std::move(on_pool)).value();
	test::check(value == 2, "线程池上的自适配图照常完成");
}

// 节点签名在某个环境下算不出来时，降解判据不能硬错误：不降解
using needs_scheduler = decltype(::sdag::adaptive::adaptive_dag<key>(
	::sdag::make<::sdag::basic_node{a}>(::stdexec::read_env(::stdexec::get_scheduler) | ::stdexec::then([](auto) { return 1; }))));
static_assert(!::stdexec::sender_in<needs_scheduler, ::stdexec::env<>>);
static_assert(!::sdag::adaptive::lowering_wanted<::stdexec::start_t, ::sdag::graph::graph_state, needs_scheduler&&, ::stdexec::env<>>());

}

int main()
{
	test_tag();
	test_lowering_trigger();
	return test::report();
}
