// 帧图的真实形状：清理节点是在 let_value 体里"晚订阅"的 exec::split。
// 兄弟失败触发 _stop_source.request_stop() 之后再订阅 split —— split 会短路。
#include "dynamic_when_all.h"

#include <exec/any_sender_of.hpp>
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

struct nap
{
	int _ms;
	auto operator()() const -> void
	{
		::std::this_thread::sleep_for(::std::chrono::milliseconds{_ms});
	}
};
struct slow_boom
{
	auto operator()() const -> void
	{
		::std::this_thread::sleep_for(::std::chrono::milliseconds{10});
		throw ::std::runtime_error{"boom"};
	}
};
struct cleanup { auto operator()() const -> void { g_cleanup.fetch_add(1); } };

auto run_once(::exec::static_thread_pool::scheduler sched, bool sibling_throws) -> void
{
	g_cleanup = 0;

	// 共享的清理节点：split，帧图里是"多个消费者共用一次执行"
	auto shared_cleanup = ex::split(ex::then(ex::just(), cleanup{}));

	::std::vector<any_snd> children;

	// 兄弟 A：10ms 后抛（或不抛）
	if (sibling_throws)
	{
		children.push_back(any_snd{ex::then(ex::starts_on(sched, ex::just()), slow_boom{})});
	}
	else
	{
		children.push_back(any_snd{ex::then(ex::starts_on(sched, ex::just()), nap{10})});
	}

	// 兄弟 B：40ms 之后才在 let_value 体里订阅那个 split —— 即"汇合点晚绑"
	children.push_back(any_snd{ex::let_value(
		ex::then(ex::starts_on(sched, ex::just()), nap{40}),
		[shared_cleanup] { return ex::then(shared_cleanup, [] {}); })});

	try { ex::sync_wait(dynamic_when_all(::std::move(children))); }
	catch (...) { }

	::std::printf("  兄弟%s : 清理跑了 %d 次%s\n",
		sibling_throws ? "抛异常" : "正常  ",
		g_cleanup.load(),
		g_cleanup.load() == 0 ? "   <<<< 清理被吞了" : "");
}

int main()
{
	::exec::static_thread_pool pool{4};
	auto sched = pool.get_scheduler();
	for (int i = 0; i < 3; ++i) { run_once(sched, false); }
	for (int i = 0; i < 3; ++i) { run_once(sched, true); }
	return 0;
}
