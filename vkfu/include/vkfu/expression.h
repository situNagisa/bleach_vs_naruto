#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

#include "./storage.h"
#include "./vulkan_object.h"

namespace vkfu
{
namespace evaluate_customization
{
void evaluate();

// Native Vulkan structures cannot grow a member `evaluate`, and an unqualified
// `evaluate` overload for them would have to sit in the global namespace. This
// prefixed name is that hook, matching vkfu::address / vkfu::set_next.
void _vkfu_evaluate();

enum class choice
{
	member,
	adl,
	prefixed,
	none,
};

struct choice_result
{
	choice selected;
	bool nothrow;
};

template<class Expression>
consteval auto choose_evaluate() noexcept -> choice_result
{
	if constexpr (requires { ::std::declval<Expression>().evaluate(); })
	{
		return {choice::member, noexcept(::std::declval<Expression>().evaluate())};
	}
	else if constexpr (requires { evaluate(::std::declval<Expression>()); })
	{
		return {choice::adl, noexcept(evaluate(::std::declval<Expression>()))};
	}
	else if constexpr (requires { _vkfu_evaluate(::std::declval<Expression>()); })
	{
		return {choice::prefixed, noexcept(_vkfu_evaluate(::std::declval<Expression>()))};
	}
	else
	{
		return {choice::none, true};
	}
}

struct evaluate_t
{
	constexpr decltype(auto) operator()(auto&& expression) const noexcept(choose_evaluate<decltype(expression)>().nothrow)
		requires (choose_evaluate<decltype(expression)>().selected != choice::none)
	{
		constexpr auto selected = choose_evaluate<decltype(expression)>().selected;
		if constexpr (selected == choice::member)
		{
			return ::std::forward<decltype(expression)>(expression).evaluate();
		}
		else if constexpr (selected == choice::adl)
		{
			return evaluate(::std::forward<decltype(expression)>(expression));
		}
		else
		{
			return _vkfu_evaluate(::std::forward<decltype(expression)>(expression));
		}
	}
};
}

inline constexpr evaluate_customization::evaluate_t evaluate{};

// Specializable, because a native Vulkan structure cannot carry a nested
// typedef. The generated header specializes it for every object it knows.
template<class T>
struct expression_vulkan_tag
{};

template<class T>
	requires requires { typename T::vulkan_tag_type; }
struct expression_vulkan_tag<T>
{
	using type = typename T::vulkan_tag_type;
};

template<class T>
	requires requires { typename expression_vulkan_tag<::std::remove_cvref_t<T>>::type; }
using expression_vulkan_tag_t = typename expression_vulkan_tag<::std::remove_cvref_t<T>>::type;

template<class T>
concept expression = requires(T t)
{
	{ evaluate(::std::forward<T>(t)) } -> storable;
	requires vulkan_object<expression_vulkan_tag_t<T>>;
};

template<expression T>
using expression_storage_t = ::std::remove_cvref_t<decltype(evaluate(::std::declval<T>()))>;

template<class T>
concept branch_expression = expression<T> && vulkan_branch_object<expression_vulkan_tag_t<T>>;

/// An expression that produces one specific vulkan object.
template<class T, class Tag>
concept expression_for = expression<T> && ::std::same_as<expression_vulkan_tag_t<T>, Tag>;
}
