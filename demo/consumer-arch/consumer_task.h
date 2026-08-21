#pragma once

#include <concepts>

#include <nagisa/concurrency/concurrency.h>

#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>

namespace nc = ::nagisa::concurrency;

struct consumer_promise;

template<class Promise, class Parent>
using consumer_awaitable = nc::build_awaitable_t<
	Promise,
	Parent,
	nc::awaitable_traits::ready_if_done,
	nc::awaitable_traits::capture_scheduler,
	nc::awaitable_traits::capture_inplace_stop_token,
	nc::awaitable_traits::this_then_parent,
	nc::awaitable_traits::run_this,
	nc::awaitable_traits::release_value,
	nc::awaitable_traits::rethrow_exception,
	nc::awaitable_traits::destroy_after_resumed>;

using consumer_task = nc::basic_task<consumer_promise, consumer_awaitable>;

using any_completions_type = ::stdexec::completion_signatures<
	::stdexec::set_value_t(),
	::stdexec::set_error_t(::std::exception_ptr),
	::stdexec::set_stopped_t()>;
using any_receiver_queries_type = ::exec::queries<::stdexec::inplace_stop_token(::stdexec::get_stop_token_t)noexcept>;
using any_receiver_type = ::exec::any_receiver<any_completions_type, any_receiver_queries_type>;
using any_sender_type = ::exec::any_sender<any_receiver_type>;
using any_scheduler_type = ::exec::any_scheduler<any_sender_type>;

/// Local task composition matching bvn::gameplay::task while keeping this demo standalone.
struct consumer_promise
	: nc::promises::lazy
	, nc::promises::exception<false>
	, nc::promises::value<void>
	// , nc::promises::propagate_stopped_to_continuation<>
	, nc::promises::jump_to_continuation<>
	, nc::promises::return_object_from_handle<consumer_promise, consumer_task>
	, nc::promises::with_scheduler<any_scheduler_type>
	, nc::promises::with_stop_token<::stdexec::inplace_stop_token>
	, nc::promises::with_await_transform<consumer_promise>
{
	using scheduler_base = nc::promises::with_scheduler<any_scheduler_type>;
	using stop_token_base = nc::promises::with_stop_token<::stdexec::inplace_stop_token>;

	constexpr explicit(false) consumer_promise() noexcept = default;

	constexpr explicit(false) consumer_promise(auto&&...)
		noexcept(::std::is_nothrow_default_constructible_v<scheduler_base>&& ::std::is_nothrow_default_constructible_v<stop_token_base>)
		requires ::std::default_initializable<scheduler_base>&& ::std::default_initializable<stop_token_base>
		: scheduler_base()
		, stop_token_base()
	{
	}

	constexpr explicit(false) consumer_promise(auto&& environment, auto&&...)
		noexcept(::std::is_nothrow_constructible_v<scheduler_base, decltype(environment)>&& ::std::is_nothrow_constructible_v<stop_token_base, decltype(environment)>)
		requires ::std::constructible_from<scheduler_base, decltype(environment)>&& ::std::constructible_from<stop_token_base, decltype(environment)>
		: scheduler_base(environment)
		, stop_token_base(environment)
	{
	}

	constexpr auto get_env() const noexcept
	{
		return ::stdexec::env{ 
			scheduler_base::get_env(),
			stop_token_base::get_env(),
			::stdexec::prop(::stdexec::get_start_scheduler, ::stdexec::get_scheduler(scheduler_base::get_env())),
		};
	}
};