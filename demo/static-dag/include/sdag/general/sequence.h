#pragma once

/// 编译期序列：一串编译期整数怎么展开成逐项调用 / 参数包。
///
///   to_array<N>(range)          编译期的 range 固定成 std::array（长度要先用另一次常量求值算出来）
///   sequence_t<Array>           std::array<size_t, N> 变成 std::index_sequence<Array[0], ..., Array[N - 1]>，供包展开使用
///   for_each_value<Array>(fn)   对编译期数组的每个元素依次调用 fn.template operator()<Array[i]>()，按下标顺序
///
/// sequence_t 与 for_each_value 有反射与模板两种实现，用 SDAG_REFLECTION 隔开。

#include <array>
#include <cstddef>
#include <ranges>
#include <utility>
#include <vector>

#include "../config.h"
#include "type_list.h"

namespace sdag::general
{

/// 把一段编译期的 range（span / vector ……）固定成 std::array（长度要先用另一次常量求值算出来）。
/// 用法：static constexpr auto fixed = to_array<range.size()>(range); 之后 fixed 可以当非类型模板实参。
template<::std::size_t N, ::std::ranges::input_range Range>
[[nodiscard]] constexpr auto to_array(Range const& values)
{
	::std::array<::std::ranges::range_value_t<Range>, N> result{};
	::std::size_t index = 0;
	for (auto const& value : values)
	{
		result[index++] = value;
	}
	return result;
}

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace detail
{

[[nodiscard]] consteval refl::info indices(::std::vector<::std::size_t> const& values)
{
	refl::infos constants;
	for (::std::size_t value : values)
	{
		constants.push_back(::std::meta::reflect_constant(value));
	}
	return ::std::meta::substitute(^^::std::index_sequence, constants);
}

}

template<auto Array>
using sequence_t = typename [: detail::indices(::std::vector<::std::size_t>(Array.begin(), Array.end())) :];

#else

namespace detail
{

template<auto Array, class Indices = ::std::make_index_sequence<Array.size()>>
struct sequence;

template<auto Array, ::std::size_t... I>
struct sequence<Array, ::std::index_sequence<I...>>
{
	using type = ::std::index_sequence<Array[I]...>;
};

}

template<auto Array>
using sequence_t = typename detail::sequence<Array>::type;

#endif

template<auto Array, class Fn>
constexpr void for_each_value(Fn&& fn)
{
#if SDAG_REFLECTION
	template for (constexpr auto value : Array)
	{
		fn.template operator()<value>();
	}
#else
	[&]<::std::size_t... I>(::std::index_sequence<I...>)
	{
		(fn.template operator()<Array[I]>(), ...);
	}(::std::make_index_sequence<Array.size()>{});
#endif
}

}

}
