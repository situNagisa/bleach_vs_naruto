#include <concepts>
#include <coroutine>
#include <functional>
#include <optional>
#include <type_traits>

struct child_promise;
struct parent_promise;

struct stop_source
{};

struct callback
{};

template <class Source, class Callback>
struct connect_stop_token
{
	Source stop_source;
	std::optional<Callback> stop_callback;
};

template <class Source, class Promise, class Parent>
struct capture_stop_token
{
	using handle_type = std::coroutine_handle<Promise>;
	using context_type = connect_stop_token<Source, callback>;

	constexpr static auto create_context(handle_type self, Parent& parent) noexcept
	{
		return context_type{};
	}
};

template <class Trait>
struct create_context_t
{
	template <class Promise, class Parent>
	constexpr decltype(auto) operator()(
		std::coroutine_handle<Promise> self,
		Parent& parent) const noexcept
		requires requires { Trait::create_context(self, parent); }
	{
		return Trait::create_context(self, parent);
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
		return std::same_as<result, connect_stop_token<stop_source, callback>>;
	}
	else
	{
		return false;
	}
}

using trait = capture_stop_token<stop_source, child_promise, parent_promise>;
using caller = create_context_t<trait>;

static_assert(check<
	caller,
	std::coroutine_handle<child_promise>,
	parent_promise&>());
