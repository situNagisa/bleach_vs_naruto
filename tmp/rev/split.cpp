// 兄弟失败时，_stop_source.request_stop() 会不会把兄弟里的 exec::split 清理节点掐掉？
#include "dynamic_when_all.h"

#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>
#include <exec/split.hpp>
#include <exec/static_thread_pool.hpp>
#include <vector>
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>

namespace ex = ::stdexec;

using sigs = ex::completion_signatures<
	ex::set_value_t(), ex::set_error_t(::std::exception_ptr), ex::set_stopped_t()>;
using any_snd = ::exec::any_sender<::exec::any_receiver<sigs>>;

inline ::std::atomic<int> g_cleanup{0};

struct slow_boom
{
	auto operator()() const -> void
	{
		::std::this_thread::sleep_for(::std::chrono::milliseconds{20});
		throw ::std::runtime_error{"boom"};
	}
};
struct cleanup { auto operator()() const -> void { g_cleanup.fetch_add(1); } };

auto run_once(::exec::static_thread_pool::scheduler sched, bool with_split) -> void
{
	g_cleanup = 0;

	// 兄弟 A：慢一点然后抛
	auto a = ex::then(ex::starts_on(sched, ex::just()), slow_boom{});

	// 兄弟 B：清理节点。with_split 时走 exec::split（帧图里共享节点的常态）
	::std::vector<any_snd> children;
	children.push_back(any_snd{::std::move(a)});

	if (with_split)
	{
		auto shared = ex::split(ex::then(ex::starts_on(sched, ex::just()), cleanup{}));
		children.push_back(any_snd{ex::then(shared, [] {})});
	}
	else
	{
		children.push_back(any_snd{ex::then(ex::starts_on(sched, ex::just()), cleanup{})});
	}

	try { ex::sync_wait(dynamic_when_all(::std::move(children))); }
	catch (...) { }

	::std::printf("  %-12s cleanup ran %d time(s)\n", with_split ? "split:" : "no split:", g_cleanup.load());
}

int main()
{
	::exec::static_thread_pool pool{4};
	auto sched = pool.get_scheduler();
	::std::printf("兄弟 A 抛异常，兄弟 B 是清理节点：\n");
	for (int i = 0; i < 5; ++i) { run_once(sched, false); }
	for (int i = 0; i < 5; ++i) { run_once(sched, true); }
	return 0;
}
