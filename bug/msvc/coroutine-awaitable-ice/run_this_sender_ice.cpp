#include <coroutine>

#include <nagisa/concurrency/coroutine.h>
#include <stdexec/execution.hpp>

namespace nc = ::nagisa::concurrency;

struct task_promise;

template <class Promise, class Parent>
using task_awaitable = nc::build_awaitable_t<
	Promise,
	Parent,
	nc::awaitable_traits::ready_if_done,
	nc::awaitable_traits::capture_scheduler,
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
	, nc::promises::without_stop_token
	, nc::promises::with_await_transform<task_promise>
{
private:
	using scheduler_base = nc::promises::with_scheduler<::stdexec::inline_scheduler>;

public:
	constexpr explicit(false) task_promise() noexcept = default;

	constexpr explicit(false) task_promise(auto&&...)
		noexcept(::std::is_nothrow_default_constructible_v<scheduler_base>)
		requires ::std::default_initializable<scheduler_base>
		: scheduler_base()
	{}

	constexpr explicit(false) task_promise(auto const& env, auto&&...)
		noexcept(::std::is_nothrow_constructible_v<scheduler_base, decltype(env)>)
		requires ::std::constructible_from<scheduler_base, decltype(env)>
		: scheduler_base(env)
	{}
};

static_assert(::stdexec::sender<task>);
