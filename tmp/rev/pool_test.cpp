// resource_pool 的行为固化。
#include <atomic>
#include <cstdio>
#include <optional>
#include <string>
#include <algorithm>
#include <exception>
#include <thread>
#include <vector>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "resource_pool.h"

namespace ex = ::stdexec;

int g_failures = 0;

auto check(char const* title, bool ok) -> void
{
	::std::printf("  %-46s %s\n", title, ok ? "ok" : "FAIL");
	if (!ok)
	{
		++g_failures;
	}
}

/// 只吸收完成信号的 receiver。`then` 的 lambda 可能抛，所以 set_error 必须有。
struct sink
{
	using receiver_concept = ex::receiver_t;

	auto set_value() noexcept -> void {}
	auto set_error(::std::exception_ptr) noexcept -> void {}
	auto set_stopped() noexcept -> void {}
};

/// 带停止令牌的 sink。
struct stoppable_sink
{
	using receiver_concept = ex::receiver_t;

	bool* _stopped;
	ex::inplace_stop_token _token;

	auto set_value() noexcept -> void {}
	auto set_error(::std::exception_ptr) noexcept -> void {}
	auto set_stopped() noexcept -> void { *_stopped = true; }

	auto get_env() const noexcept { return ex::prop{ex::get_stop_token, _token}; }
};

struct token
{
	int _id = 0;
	static ::std::atomic<int> alive;

	explicit token(int id) : _id(id) { alive.fetch_add(1); }
	token(token&& other) noexcept : _id(other._id) { alive.fetch_add(1); }
	~token() { alive.fetch_sub(1); }
};
::std::atomic<int> token::alive{0};

auto make_pool(::std::size_t count)
{
	return resource_pool<token>{count, [](::std::size_t index) { return token{static_cast<int>(index)}; }};
}

int main()
{
	::std::printf("resource_pool\n");

	// 1. 有空闲：同步完成
	{
		auto pool = make_pool(2);
		auto result = ex::sync_wait(pool.acquire());
		check("1 有空闲 · 拿到租约", result.has_value());
		check("1 有空闲 · 租约作用域内不还", pool.available() == 1);
		result.reset();
		check("1 有空闲 · 租约析构即归还", pool.available() == 2);
	}

	// 2. 单槽：第二个申请排队，第一个归还后直接转交
	{
		auto pool = make_pool(1);
		auto order = ::std::vector<::std::string>{};

		auto first = ex::sync_wait(pool.acquire());
		check("2 单槽 · 第一个立刻拿到", first.has_value());

		auto second = ::std::optional<resource_pool<token>::lease>{};
		auto queued = ex::connect(
			pool.acquire() | ex::then([&](auto lease) { order.push_back("second"); second.emplace(::std::move(lease)); }),
			sink{});
		ex::start(queued);
		check("2 单槽 · 第二个在排队", !second.has_value() && pool.available() == 0);

		order.push_back("release");
		first.reset();
		check("2 单槽 · 归还即转交给等待者", second.has_value());
		check("2 单槽 · 转交时资源没回空闲表", pool.available() == 0);
		check("2 单槽 · 续体在归还线程上跑完",
			order.size() == 2 && order[0] == "release" && order[1] == "second");
		second.reset();
		check("2 单槽 · 最后归还", pool.available() == 1);
	}

	// 3. 先来先到
	{
		auto pool = make_pool(1);
		auto seen = ::std::vector<int>{};
		auto held = ex::sync_wait(pool.acquire());

		auto make = [&](int id)
		{
			return ex::connect(
				pool.acquire() | ex::then([&seen, id](auto) { seen.push_back(id); }), sink{});
		};
		auto a = make(1);
		auto b = make(2);
		auto c = make(3);
		ex::start(a);
		ex::start(b);
		ex::start(c);

		held.reset();
		check("3 先来先到", seen.size() == 3 && seen[0] == 1 && seen[1] == 2 && seen[2] == 3);
	}

	// 4. try_acquire 不排队
	{
		auto pool = make_pool(1);
		auto first = pool.try_acquire();
		check("4 try_acquire · 有空闲时给", first.has_value());
		check("4 try_acquire · 没空闲时返回 nullopt", !pool.try_acquire().has_value());
		first.reset();
		check("4 try_acquire · 归还后又能拿", pool.try_acquire().has_value());
	}

	// 5. 排队期间被取消
	{
		auto pool = make_pool(1);
		auto held = ex::sync_wait(pool.acquire());

		auto source = ex::inplace_stop_source{};
		auto stopped = false;
		auto got = false;

		auto queued = ex::connect(
			pool.acquire() | ex::then([&got](auto) { got = true; }),
			stoppable_sink{&stopped, source.get_token()});
		ex::start(queued);
		check("5 取消 · 先在排队", !stopped && !got);

		source.request_stop();
		check("5 取消 · 摘掉并 set_stopped", stopped && !got);

		held.reset();
		check("5 取消 · 归还后资源回空闲表（没转交给已取消的）", pool.available() == 1);
	}

	// 6. 一开始就已取消：拿到的还是租约（有空闲就不该无谓失败）
	{
		auto pool = make_pool(1);
		auto source = ex::inplace_stop_source{};
		source.request_stop();

		auto got = false;
		auto stopped = false;
		auto op = ex::connect(pool.acquire() | ex::then([&got](auto) { got = true; }),
			stoppable_sink{&stopped, source.get_token()});
		ex::start(op);
		check("6 预先取消但有空闲 · 照样给", got && !stopped);
	}

	// 7. 归还的续体里又去 acquire（重入）
	{
		auto pool = make_pool(1);
		auto depth = 0;
		auto reached = 0;

		// 三个排队者，每个拿到就立刻放掉 -> 触发下一个
		auto lease_slots = ::std::vector<::std::optional<resource_pool<token>::lease>>(3);
		auto make = [&](int index)
		{
			return ex::connect(pool.acquire() | ex::then([&, index](auto lease)
			{
				++reached;
				depth = ::std::max(depth, index);
				lease_slots[static_cast<::std::size_t>(index)].emplace(::std::move(lease));
				lease_slots[static_cast<::std::size_t>(index)].reset();   // 归还 -> 唤醒下一个
			}), sink{});
		};
		auto held = ex::sync_wait(pool.acquire());
		auto a = make(0);
		auto b = make(1);
		auto c = make(2);
		ex::start(a);
		ex::start(b);
		ex::start(c);
		held.reset();
		check("7 重入 · 归还续体里再 acquire", reached == 3 && pool.available() == 1);
	}

	// 8. 多线程抢：单槽 8 线程各跑 50 次，一次不漏也不重
	{
		auto pool = make_pool(1);
		auto counter = ::std::atomic<int>{0};
		auto overlap = ::std::atomic<int>{0};
		auto inside = ::std::atomic<int>{0};

		auto workers = ::std::vector<::std::thread>{};
		for (auto worker = 0; worker != 8; ++worker)
		{
			workers.emplace_back([&]
			{
				for (auto round = 0; round != 50; ++round)
				{
					ex::sync_wait(pool.acquire() | ex::then([&](auto)
					{
						if (inside.fetch_add(1) != 0)
						{
							overlap.fetch_add(1);
						}
						counter.fetch_add(1);
						inside.fetch_sub(1);
					}));
				}
			});
		}
		for (auto&& worker : workers)
		{
			worker.join();
		}
		check("8 多线程 · 全部完成", counter.load() == 400);
		check("8 多线程 · 从不并发持有同一份", overlap.load() == 0);
		check("8 多线程 · 最后全部归还", pool.available() == 1);
	}

	// 9. 接在 scheduler 后面：使用者自己决定续体在哪跑
	{
		auto workers = ::exec::static_thread_pool{2};
		auto pool = make_pool(2);
		auto main_thread = ::std::this_thread::get_id();
		auto seen = ::std::thread::id{};

		ex::sync_wait(pool.acquire()
			| ex::continues_on(workers.get_scheduler())
			| ex::then([&](auto) { seen = ::std::this_thread::get_id(); }));
		check("9 continues_on · 续体换线程", seen != ::std::thread::id{} && seen != main_thread);
	}

	// 10. 资源没泄漏
	{
		{
			auto pool = make_pool(4);
			check("10 资源 · 池子里 4 份", token::alive.load() == 4);
		}
		check("10 资源 · 池子销毁后清零", token::alive.load() == 0);
	}

	::std::printf("%s\n", g_failures == 0 ? "全部通过" : "有失败");
	return g_failures == 0 ? 0 : 1;
}
