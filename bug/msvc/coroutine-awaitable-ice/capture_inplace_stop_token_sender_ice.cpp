#include <coroutine>

#include <nagisa/concurrency/coroutine.h>
#include <stdexec/execution.hpp>

namespace nc = ::nagisa::concurrency;

struct task_promise;

template <class Promise, class Parent>
struct resume_this
{
	using handle_type = ::std::coroutine_handle<Promise>;

	constexpr static handle_type await_suspend(handle_type self, ::std::coroutine_handle<Parent>) noexcept
	{
		return self;
	}
};

template <class Promise, class Parent>
using task_awaitable = nc::build_awaitable_t<
	Promise,
	Parent,
	nc::awaitable_traits::ready_if_done,
	nc::awaitable_traits::capture_scheduler,
	nc::awaitable_traits::capture_inplace_stop_token,
	nc::awaitable_traits::this_then_parent,
	resume_this,
	nc::awaitable_traits::release_value,
	nc::awaitable_traits::rethrow_exception,
	nc::awaitable_traits::destroy_after_resumed>;

using task = nc::basic_task<task_promise, task_awaitable>;

struct task_promise
	: nc::promises::lazy
	, nc::promises::exception<true>
	, nc::promises::value<void>
	, nc::promises::jump_to_continuation<>
	, nc::promises::return_object_from_handle<task_promise, task>
	, nc::promises::with_scheduler<::stdexec::inline_scheduler>
	, nc::promises::with_stop_token<::stdexec::inplace_stop_token>
	, nc::promises::with_await_transform<task_promise>
{
private:
	using scheduler_base = nc::promises::with_scheduler<::stdexec::inline_scheduler>;
	using stop_token_base = nc::promises::with_stop_token<::stdexec::inplace_stop_token>;

public:
	constexpr explicit(false) task_promise() noexcept = default;

	constexpr explicit(false) task_promise(auto const& env, auto&&...)
		noexcept(
			::std::is_nothrow_constructible_v<scheduler_base, decltype(env)>
			&& ::std::is_nothrow_constructible_v<stop_token_base, decltype(env)>)
		requires
			::std::constructible_from<scheduler_base, decltype(env)>
			&& ::std::constructible_from<stop_token_base, decltype(env)>
		: scheduler_base(env)
		, stop_token_base(env)
	{}

	constexpr auto get_env() const noexcept
	{
		return ::stdexec::env{scheduler_base::get_env(), stop_token_base::get_env()};
	}
};

static_assert(::stdexec::sender<task>);
