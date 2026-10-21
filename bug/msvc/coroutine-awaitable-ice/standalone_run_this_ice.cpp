#include <concepts>
#include <coroutine>
#include <functional>
#include <type_traits>
#include <utility>

struct child_promise;
struct parent_promise;

template <class Promise, class Parent>
struct run_this
{
	constexpr static decltype(auto) await_suspend(
		std::coroutine_handle<Promise> self,
		std::coroutine_handle<Parent> parent) noexcept
	{
		return self;
	}
};

template <class Trait>
struct await_suspend_t
{
	template <class Promise, class Parent>
	constexpr decltype(auto) operator()(
		std::coroutine_handle<Promise> self,
		std::coroutine_handle<Parent> parent) const noexcept
		requires requires { Trait::await_suspend(self, parent); }
	{
		return Trait::await_suspend(self, parent);
	}
};

template <class Callable, class... Args>
concept callable = std::invocable<Callable, Args...>;

template <class Callable, class... Args>
constexpr auto check() noexcept
{
	if constexpr (callable<Callable, Args...>)
	{
		using result = std::invoke_result_t<Callable, Args...>;
		return std::same_as<result, void> || std::same_as<result, std::coroutine_handle<child_promise>>;
	}
	else
	{
		return false;
	}
}

using trait = run_this<child_promise, parent_promise>;
using caller = await_suspend_t<trait>;

static_assert(check<
	caller,
	std::coroutine_handle<child_promise>,
	std::coroutine_handle<parent_promise>>());
