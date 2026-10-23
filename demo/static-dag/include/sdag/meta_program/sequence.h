#pragma once

/// sequence_t<Array>：编译期的 std::array<size_t, N> 变成 std::index_sequence<Array[0], ..., Array[N - 1]>，供包展开使用。

#include <cstddef>
#include <utility>
#include <vector>

#include "../config.h"
#include "../meta_program/type_list.h"
#include "../meta_program/utility.h"
#include "../meta_program/indexed_tuple.h"
#include "../meta_program/reflection.h"

namespace sdag::meta
{

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

}

}
