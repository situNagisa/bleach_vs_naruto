#include "dynamic_when_all.h"

#include <exec/any_sender_of.hpp>
#include <exec/static_thread_pool.hpp>
#include <vector>
#include <cstdio>
#include <atomic>
#include <ranges>

namespace ex = ::stdexec;

using sigs = ex::completion_signatures<
	ex::set_value_t(), ex::set_error_t(::std::exception_ptr), ex::set_stopped_t()>;
using any_snd = ::exec::any_sender<::exec::any_receiver<sigs>>;

inline ::std::atomic<int> g_ran{0};
inline ::std::atomic<int> g_copies{0};
inline ::std::atomic<int> g_moves{0};

struct tick
{
	int _id = 0;
	tick() = default;
	explicit tick(int id) : _id(id) {}
	tick(tick const& other) : _id(other._id) { g_copies.fetch_add(1); }
	tick(tick&& other) noexcept : _id(other._id) { other._id = -1; g_moves.fetch_add(1); }
	auto operator=(tick const&) -> tick& = default;
	auto operator=(tick&&) -> tick& = default;
	auto operator()() const -> void { g_ran.fetch_add(1); }
};
struct boom { auto operator()() const -> void { throw ::std::runtime_error{"boom"}; } };

using plain = decltype(ex::then(ex::just(), tick{}));

auto reset() -> void { g_ran = 0; g_copies = 0; g_moves = 0; }

int main()
{
	// 1. lvalue vector of copyable senders -> whole container copied
	{
		::std::vector<plain> children;
		for (int i = 0; i < 3; ++i) { children.push_back(ex::then(ex::just(), tick{i})); }
		reset();
		ex::sync_wait(dynamic_when_all(children));
		::std::printf("1 lvalue vector : ran=%d copies=%d moves=%d caller-size=%zu\n",
			g_ran.load(), g_copies.load(), g_moves.load(), children.size());
	}
	// 2. rvalue vector -> moved in
	{
		::std::vector<plain> children;
		for (int i = 0; i < 3; ++i) { children.push_back(ex::then(ex::just(), tick{i})); }
		reset();
		ex::sync_wait(dynamic_when_all(::std::move(children)));
		::std::printf("2 rvalue vector : ran=%d copies=%d moves=%d\n",
			g_ran.load(), g_copies.load(), g_moves.load());
	}
	// 3. views::all(lvalue) == ref_view -> no container copy, but elements moved OUT of caller
	{
		::std::vector<plain> children;
		for (int i = 0; i < 3; ++i) { children.push_back(ex::then(ex::just(), tick{i})); }
		reset();
		ex::sync_wait(dynamic_when_all(::std::views::all(children)));
		::std::printf("3 ref_view      : ran=%d copies=%d moves=%d caller-size=%zu\n",
			g_ran.load(), g_copies.load(), g_moves.load(), children.size());
		reset();
		ex::sync_wait(dynamic_when_all(::std::views::all(children)));
		::std::printf("3b same vector again : ran=%d  <- caller's senders were stolen\n",
			g_ran.load());
	}
	// 4. move-only elements + lvalue vector: constraint-rejected, or hard error?
	{
		::std::vector<any_snd> children;
		constexpr bool callable = requires { dynamic_when_all(children); };
		::std::printf("4 moveonly lvalue : constraint-callable=%s\n", callable ? "TRUE" : "false");
	}
	// 5. empty
	{
		::std::vector<any_snd> children;
		ex::sync_wait(dynamic_when_all(::std::move(children)));
		::std::printf("5 empty         : ok\n");
	}
	// 6. error
	{
		::std::vector<any_snd> children;
		children.push_back(any_snd{ex::then(ex::just(), tick{0})});
		children.push_back(any_snd{ex::then(ex::just(), boom{})});
		try { ex::sync_wait(dynamic_when_all(::std::move(children))); ::std::printf("6 FAIL\n"); }
		catch (::std::exception const& e) { ::std::printf("6 error        : %s\n", e.what()); }
	}
	// 7. 64 children on a pool
	{
		::exec::static_thread_pool pool{4};
		auto sched = pool.get_scheduler();
		::std::vector<any_snd> children;
		for (int i = 0; i < 64; ++i)
		{
			children.push_back(any_snd{ex::then(ex::starts_on(sched, ex::just()), tick{i})});
		}
		reset();
		ex::sync_wait(dynamic_when_all(::std::move(children)));
		::std::printf("7 pool         : ran=%d (expect 64)\n", g_ran.load());
	}
	// 8. transform_view producing prvalue senders
	{
		::std::vector<int> ids{1, 2, 3, 4};
		reset();
		auto view = ids | ::std::views::transform([](int i) { return ex::then(ex::just(), tick{i}); });
		ex::sync_wait(dynamic_when_all(view));
		::std::printf("8 transform    : ran=%d (expect 4)\n", g_ran.load());
	}
	return 0;
}
