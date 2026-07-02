#pragma once

#include <nagisa/concurrency/coroutine.h>
#include <stdexec/execution.hpp>

namespace bvn::gameplay
{
namespace nc = ::nagisa::concurrency;

struct task_promise;

template <class promise_type, class parent_type>
using task_awaitable = nc::build_awaitable_t<
	promise_type,
	parent_type,
	nc::awaitable_traits::ready_if_done,
	nc::awaitable_traits::capture_scheduler,
	nc::awaitable_traits::capture_inplace_stop_token,
	nc::awaitable_traits::this_then_parent,
	nc::awaitable_traits::run_this,
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
	using scheduler_base = nc::promises::with_scheduler<::stdexec::inline_scheduler>;
	using stop_token_base = nc::promises::with_stop_token<::stdexec::inplace_stop_token>;

	constexpr explicit(false) task_promise() noexcept = default;

	constexpr explicit(false) task_promise(auto&&...)
		noexcept(::std::is_nothrow_default_constructible_v<scheduler_base> && ::std::is_nothrow_default_constructible_v<stop_token_base>)
		requires ::std::default_initializable<scheduler_base> && ::std::default_initializable<stop_token_base>
		: scheduler_base()
		, stop_token_base()
	{}

	constexpr explicit(false) task_promise(auto&& env, auto&&...)
		noexcept(::std::is_nothrow_constructible_v<scheduler_base, decltype(env)> && ::std::is_nothrow_constructible_v<stop_token_base, decltype(env)>)
		requires ::std::constructible_from<scheduler_base, decltype(env)> && ::std::constructible_from<stop_token_base, decltype(env)>
		: scheduler_base(env)
		, stop_token_base(env)
	{}

	constexpr auto get_env() const noexcept
	{
		return ::stdexec::env{scheduler_base::get_env(), stop_token_base::get_env()};
	}
};

static_assert(::stdexec::sender<task>);
}
