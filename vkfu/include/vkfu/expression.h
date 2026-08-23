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

enum class choice
{
	member,
	adl,
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
		else
		{
			return evaluate(::std::forward<decltype(expression)>(expression));
		}
	}
};
}

inline constexpr evaluate_customization::evaluate_t evaluate{};

template<class T>
	requires requires { typename ::std::remove_cvref_t<T>::vulkan_tag_type; }
using expression_vulkan_tag_t = typename ::std::remove_cvref_t<T>::vulkan_tag_type;

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
}
