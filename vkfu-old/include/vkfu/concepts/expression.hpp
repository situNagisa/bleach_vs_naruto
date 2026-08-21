#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

#include "./vulkan_object.hpp"

namespace vkfu
{
enum class expression_category
{
	root,
	branch,
	feature,
};

template<class T>
struct expression_traits;

template<class T>
	requires requires
	{
		typename expression_traits<::std::remove_cvref_t<T>>::result_type;
	}
using expression_result_t =
	typename expression_traits<::std::remove_cvref_t<T>>::result_type;

template<class T>
	requires requires
	{
		{ expression_traits<::std::remove_cvref_t<T>>::category }
			-> ::std::convertible_to<expression_category>;
	}
inline constexpr expression_category expression_category_v =
	expression_traits<::std::remove_cvref_t<T>>::category;

template<class T> requires requires{ typename ::std::remove_cvref_t<T>::storage_type; }
using expression_storage_t = ::std::remove_cvref_t<T>::storage_type;

template<class T>
concept expression = requires(T t)
{
	requires ::std::semiregular<expression_storage_t<T>>;
	requires vulkan_object<expression_result_t<T>>;
	requires ::std::same_as<
		decltype(expression_category_v<T>),
		expression_category const>;
};
}
