#include "dynamic_when_all.h"

#include <exec/any_sender_of.hpp>
#include <exec/static_thread_pool.hpp>
#include <vector>
#include <cstdio>
#include <atomic>

namespace ex = ::stdexec;

using sigs = ex::completion_signatures<
	ex::set_value_t(), ex::set_error_t(::std::exception_ptr), ex::set_stopped_t()>;
using any_snd = ::exec::any_sender<::exec::any_receiver<sigs>>;

inline ::std::atomic<int> g_ran{0};
struct tick { auto operator()() const -> void { g_ran.fetch_add(1); } };
using plain = decltype(ex::then(ex::just(), tick{}));

// 一个不会停的 receiver（env 里没有 stop token → never_stop_token）
struct null_receiver
{
	using receiver_concept = ex::receiver_t;
	auto set_value() noexcept -> void {}
	auto set_error(::std::exception_ptr) noexcept -> void {}
	auto set_stopped() noexcept -> void {}
};

// 一个会停的 receiver
struct stoppable_receiver
{
	using receiver_concept = ex::receiver_t;
	ex::inplace_stop_token _token;
	auto set_value() noexcept -> void {}
	auto set_error(::std::exception_ptr) noexcept -> void {}
	auto set_stopped() noexcept -> void {}
	auto get_env() const noexcept { return ex::prop{ex::get_stop_token, _token}; }
};

using unstoppable_state = dynamic_when_all_state<null_receiver>;
using stoppable_state   = dynamic_when_all_state<stoppable_receiver>;

int main()
{
	static_assert(!unstoppable_state::_uses_stop_callback, "never_stop_token 应该掐掉回调");
	static_assert(stoppable_state::_uses_stop_callback, "inplace_stop_token 应该保留回调");

	using child_op = ex::connect_result_t<plain, dynamic_when_all_child_receiver<null_receiver>>;
	::std::printf("sizeof(child op-state)       = %zu\n", sizeof(child_op));
	::std::printf("sizeof(manual_lifetime<op>)  = %zu   <- 和上面相等即零额外开销\n",
		sizeof(manual_lifetime<child_op>));
	static_assert(sizeof(manual_lifetime<child_op>) == sizeof(child_op));
	static_assert(alignof(manual_lifetime<child_op>) == alignof(child_op));

	::std::printf("sizeof(state<不会停>)        = %zu\n", sizeof(unstoppable_state));
	::std::printf("sizeof(state<会停>)          = %zu   <- 差额就是省掉的 optional<callback>\n",
		sizeof(stoppable_state));
	static_assert(sizeof(unstoppable_state) < sizeof(stoppable_state));

	{
		using op_t = ex::connect_result_t<
			decltype(dynamic_when_all(::std::declval<::std::vector<plain>>())), null_receiver>;
		::std::printf("op-state 比 state 多出来的 = %zu   <- 等于 sizeof(void*) 即只剩那个 unique_ptr\n",
			sizeof(op_t) - sizeof(unstoppable_state));
		static_assert(sizeof(op_t) - sizeof(unstoppable_state) == sizeof(void*));
	}

	// 空 range 不分配：直接完成，不碰堆
	{
		::std::vector<any_snd> empty;
		auto op = ex::connect(dynamic_when_all(::std::move(empty)), null_receiver{});
		::std::printf("empty _operations pointer    = %s\n", op._operations ? "非空(有分配)" : "nullptr(无分配)");
		if (op._operations) { return 1; }
		ex::start(op);
	}

	// 压一遍
	::exec::static_thread_pool pool{4};
	auto sched = pool.get_scheduler();
	int bad = 0;
	for (int round = 0; round < 200; ++round)
	{
		::std::vector<any_snd> children;
		for (int i = 0; i < 16; ++i)
		{
			children.push_back(any_snd{ex::then(ex::starts_on(sched, ex::just()), tick{})});
		}
		g_ran = 0;
		ex::sync_wait(dynamic_when_all(::std::move(children)));
		if (g_ran.load() != 16) { ++bad; }
	}
	::std::printf("stress 200x16                = %s\n", bad == 0 ? "200/200 通过" : "有失败");
	return bad == 0 ? 0 : 1;
}
