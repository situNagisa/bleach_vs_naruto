#include <coroutine>

#include <nagisa/concurrency/coroutine.h>
#include <stdexec/execution.hpp>

namespace nc = ::nagisa::concurrency;

struct task_promise;

template <class Promise, class Parent>
using task_awaitable = nc::build_awaitable_t<
	Promise,
	Parent,
	nc::awaitable_traits::capture_inplace_stop_token>;

using task = nc::basic_task<task_promise, task_awaitable>;

struct task_promise
	: nc::promises::lazy
	, nc::promises::exception<true>
	, nc::promises::value<void>
	, nc::promises::jump_to_continuation<>
	, nc::promises::return_object_from_handle<task_promise, task>
	, nc::promises::with_stop_token<::stdexec::inplace_stop_token>
{
private:
	using stop_token_base = nc::promises::with_stop_token<::stdexec::inplace_stop_token>;

public:
	constexpr explicit(false) task_promise() noexcept = default;

	constexpr explicit(false) task_promise(auto const& env, auto&&...)
		noexcept(::std::is_nothrow_constructible_v<stop_token_base, decltype(env)>)
		requires ::std::constructible_from<stop_token_base, decltype(env)>
		: stop_token_base(env)
	{}

	constexpr auto get_env() const noexcept
	{
		return stop_token_base::get_env();
	}
};

static_assert(::stdexec::sender<task>);
