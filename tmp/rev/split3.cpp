// 同上，但不走类型擦除 —— 让 stop token 真的能从 child_receiver 的 env 流进 split 的订阅。
// 两个孩子同一个类型：let_value(then(starts_on(...), phase1), body)
#include "dynamic_when_all.h"

#include <exec/split.hpp>
#include <exec/static_thread_pool.hpp>
#include <vector>
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>

namespace ex = ::stdexec;

inline ::std::atomic<int> g_cleanup{0};

struct phase1
{
	int _ms;
	bool _throws;
	auto operator()() const -> void
	{
		::std::this_thread::sleep_for(::std::chrono::milliseconds{_ms});
		if (_throws) { throw ::std::runtime_error{"boom"}; }
	}
};
struct cleanup { auto operator()() const -> void { g_cleanup.fetch_add(1); } };
struct noop { auto operator()() const -> void {} };

using shared_type = decltype(::exec::split(ex::then(ex::just(), cleanup{})));

struct body
{
	shared_type _shared;
	auto operator()() const { return ex::then(_shared, noop{}); }
};

auto run_once(::exec::static_thread_pool::scheduler sched, bool sibling_throws) -> void
{
	g_cleanup = 0;
	auto shared = ::exec::split(ex::then(ex::just(), cleanup{}));

	auto make = [&](int ms, bool throws) {
		return ex::let_value(
			ex::then(ex::starts_on(sched, ex::just()), phase1{ms, throws}),
			body{shared});
	};

	::std::vector<decltype(make(0, false))> children;
	children.push_back(make(10, sibling_throws));  // 兄弟 A：10ms 后（可能）抛
	children.push_back(make(40, false));           // 兄弟 B：40ms 后才订阅那个 split

	try { ex::sync_wait(dynamic_when_all(::std::move(children))); }
	catch (...) { }

	::std::printf("  兄弟%s : 清理跑了 %d 次%s\n",
		sibling_throws ? "抛异常" : "正常  ",
		g_cleanup.load(),
		(sibling_throws && g_cleanup.load() == 0) ? "   <<<< 清理被吞了" : "");
}

int main()
{
	::exec::static_thread_pool pool{4};
	auto sched = pool.get_scheduler();
	for (int i = 0; i < 3; ++i) { run_once(sched, false); }
	for (int i = 0; i < 3; ++i) { run_once(sched, true); }
	return 0;
}
