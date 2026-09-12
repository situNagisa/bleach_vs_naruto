// let_stopped 够不够让"必须跑完的收尾"真的跑完？
//
// 形状按真实帧图来（收尾在下游，不是录制的兄弟）：
//   兄弟A ────────────────────────── 抛异常
//   兄弟B: begin → 录制 → fence → [变体] → wait_fence → give_back
// 兄弟A 失败 → dynamic_when_all 广播 stop → 看 give_back 还跑不跑。
#include "dynamic_when_all.h"

#include <exec/split.hpp>
#include <exec/static_thread_pool.hpp>
#include <vector>
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>

namespace ex = ::stdexec;

inline ::std::atomic<int> g_wait_fence{0};
inline ::std::atomic<int> g_give_back{0};

struct nap
{
	int _ms;
	auto operator()() const -> void { ::std::this_thread::sleep_for(::std::chrono::milliseconds{_ms}); }
};
struct boom
{
	auto operator()() const -> void
	{
		::std::this_thread::sleep_for(::std::chrono::milliseconds{10});
		throw ::std::runtime_error{"boom"};
	}
};
struct wait_fence { auto operator()() const -> void { g_wait_fence.fetch_add(1); } };
struct give_back { auto operator()() const -> void { g_give_back.fetch_add(1); } };

template <class TailFactory>
auto run(char const* label, ::exec::static_thread_pool::scheduler sched, TailFactory&& make_body) -> void
{
	g_wait_fence = 0;
	g_give_back = 0;

	using child_type = decltype(make_body());
	::std::vector<child_type> children;
	children.push_back(make_body());   // 兄弟B：正常走完 40ms 再进尾巴
	children.push_back(make_body());   // 占位，保证同类型

	// 兄弟A 用同一个类型装不下，改成：直接把 stop 提前请求掉，等价于"兄弟已经失败"
	ex::inplace_stop_source source;
	source.request_stop();

	auto graph = ex::write_env(dynamic_when_all(::std::move(children)),
		ex::prop{ex::get_stop_token, source.get_token()});

	try { ex::sync_wait(::std::move(graph)); }
	catch (...) { }

	::std::printf("  %-34s wait_fence=%d give_back=%d %s\n",
		label, g_wait_fence.load(), g_give_back.load(),
		g_give_back.load() == 0 ? "  <<<< 收尾没跑" : "");
}

int main()
{
	::exec::static_thread_pool pool{4};
	auto sched = pool.get_scheduler();

	::std::printf("外层 token 已 stopped（等价于兄弟已失败并广播）：\n");

	// 变体 1：什么都不做
	{
		auto shared = ::exec::split(ex::then(ex::starts_on(sched, ex::just()), wait_fence{}));
		run("1 裸的", sched, [&] {
			return ex::let_value(ex::then(ex::starts_on(sched, ex::just()), nap{5}),
				[shared] { return ex::then(shared, give_back{}); });
		});
	}
	// 变体 2：加 let_stopped —— 上游被停掉时兜住，继续订阅收尾
	{
		auto shared = ::exec::split(ex::then(ex::starts_on(sched, ex::just()), wait_fence{}));
		run("2 let_stopped 兜住", sched, [&] {
			return ex::let_stopped(
				ex::let_value(ex::then(ex::starts_on(sched, ex::just()), nap{5}),
					[shared] { return ex::then(shared, give_back{}); }),
				[shared] { return ex::then(shared, give_back{}); });
		});
	}
	// 变体 3：只给收尾钉一个不会停的 token
	{
		auto shared = ::exec::split(ex::then(ex::starts_on(sched, ex::just()), wait_fence{}));
		run("3 write_env 钉住 token", sched, [&] {
			return ex::let_value(ex::then(ex::starts_on(sched, ex::just()), nap{5}),
				[shared] {
					return ex::write_env(ex::then(shared, give_back{}),
						ex::prop{ex::get_stop_token, ex::never_stop_token{}});
				});
		});
	}
	// 变体 4：两个都上
	{
		auto shared = ::exec::split(ex::then(ex::starts_on(sched, ex::just()), wait_fence{}));
		auto pinned = [shared] {
			return ex::write_env(ex::then(shared, give_back{}),
				ex::prop{ex::get_stop_token, ex::never_stop_token{}});
		};
		run("4 let_stopped + write_env", sched, [&] {
			return ex::let_stopped(
				ex::let_value(ex::then(ex::starts_on(sched, ex::just()), nap{5}), pinned),
				pinned);
		});
	}
	return 0;
}
