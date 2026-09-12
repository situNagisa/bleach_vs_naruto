// when_all 的行为有没有被补丁改掉：值 / 错误 / 停止 / error 压 stopped / 兄弟广播 / 外部取消
// 用自带 receiver，避开 sync_wait 对"必须有 value 通道"的要求。
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <atomic>
#include <cstdio>
#include <exception>
#include <thread>
#include <chrono>
#include <string_view>

namespace ex = stdexec;

int failures = 0;

void check(bool ok, char const* what)
{
	std::printf("  %-48s %s\n", what, ok ? "ok" : "FAIL");
	if (!ok) { ++failures; }
}

struct recording_receiver
{
	using receiver_concept = ex::receiver_t;

	char const** channel;
	std::atomic<int>* count;
	ex::inplace_stop_token token;

	template <class... Args>
	void set_value(Args&&...) noexcept { *channel = "value"; count->fetch_add(1); }
	void set_error(std::exception_ptr) noexcept { *channel = "error"; count->fetch_add(1); }
	template <class Error>
	void set_error(Error&&) noexcept { *channel = "error"; count->fetch_add(1); }
	void set_stopped() noexcept { *channel = "stopped"; count->fetch_add(1); }

	auto get_env() const noexcept { return ex::prop{ex::get_stop_token, token}; }
};

template <class Sender>
char const* channel_of(Sender&& sender, ex::inplace_stop_token token = {})
{
	char const* channel = "(none)";
	std::atomic<int> count{0};
	auto op = ex::connect(static_cast<Sender&&>(sender),
		recording_receiver{&channel, &count, token});
	ex::start(op);
	// 操作可能是异步的：必须等它完成，否则 op 在这里就被析构了
	while (count.load(std::memory_order_acquire) == 0)
	{
		std::this_thread::sleep_for(std::chrono::microseconds{200});
	}
	std::this_thread::sleep_for(std::chrono::milliseconds{2});
	if (count.load(std::memory_order_relaxed) != 1) { return "COMPLETED-NOT-ONCE"; }
	return channel;
}

struct thrower { auto operator()() const -> void { throw std::runtime_error{"boom"}; } };

int main()
{
	check(std::string_view{channel_of(ex::when_all(ex::just(1), ex::just(2)))} == "value",
		"全部成功 -> set_value");

	check(std::string_view{channel_of(ex::when_all(ex::just(), ex::then(ex::just(), thrower{})))} == "error",
		"有孩子 set_error -> set_error");

	check(std::string_view{channel_of(ex::when_all(ex::just(), ex::just_stopped()))} == "stopped",
		"有孩子 set_stopped -> set_stopped");

	check(std::string_view{channel_of(ex::when_all(
			ex::just_stopped(), ex::then(ex::just(), thrower{})))} == "error",
		"error 压过 stopped");

	{
		ex::inplace_stop_source source;
		source.request_stop();
		auto const got = channel_of(ex::when_all(ex::just(), ex::just_stopped()), source.get_token());
		check(std::string_view{got} == "stopped", "外部 token 预先 stopped -> set_stopped");
	}

	// 兄弟报错要把别的兄弟取消掉（内部 source 广播）
	{
		exec::static_thread_pool pool{4};
		auto sched = pool.get_scheduler();
		std::atomic<bool> sibling_saw_stop{false};

		auto probe = ex::let_value(ex::schedule(sched), [&] {
			return ex::read_env(ex::get_stop_token) | ex::then([&](auto token) {
				for (int i = 0; i < 3000; ++i)
				{
					if (token.stop_requested()) { sibling_saw_stop = true; return; }
					std::this_thread::sleep_for(std::chrono::microseconds{100});
				}
			});
		});
		auto boom = ex::then(ex::schedule(sched), [] {
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
			throw std::runtime_error{"boom"};
		});

		auto const got = channel_of(ex::when_all(std::move(boom), std::move(probe)));
		check(std::string_view{got} == "error", "兄弟报错 -> 整体 set_error");
		check(sibling_saw_stop.load(), "兄弟报错 -> 其余兄弟的 token 被 request_stop");
	}

	std::printf("%s\n", failures == 0 ? "全部通过" : "有失败");
	return failures == 0 ? 0 : 1;
}
