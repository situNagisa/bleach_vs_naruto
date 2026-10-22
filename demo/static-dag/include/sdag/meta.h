#pragma once

/// 编译期工具：类型列表的运算全部用 C++26 静态反射（P2996）写成普通的 consteval 函数，
/// 类型层面只留下把 std::meta::info 拼回类型的几个别名。运行期容器 indexed_tuple 保持模板。

#if !defined(__cpp_impl_reflection)
#error "sdag 需要 C++26 静态反射（P2996）：请用 g++ >= 16 并打开 -freflection"
#endif

#include <array>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>
#include <vector>

/// MSVC ABI 下 [[no_unique_address]] 被忽略，要用 [[msvc::no_unique_address]]；
/// 多个空基类也只有第一个能做空基类优化，要加 __declspec(empty_bases)。
#if defined(_MSC_VER)
#define SDAG_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#define SDAG_EMPTY_BASES __declspec(empty_bases)
#else
#define SDAG_NO_UNIQUE_ADDRESS [[no_unique_address]]
#define SDAG_EMPTY_BASES
#endif

namespace sdag::meta
{

using info = ::std::meta::info;
using infos = ::std::vector<info>;

/// 被优化掉的成员用它占位。indexed_tuple 对它特化成真正的空基类，不占空间。
struct empty
{
};

/// 同一个类里多个被优化掉的成员要用互不相同的空类型，否则 no_unique_address 也得给它们分配不同地址。
template<class Tag>
struct empty_for
{
};

template<class... Ts>
struct type_list
{
	static constexpr ::std::size_t size = sizeof...(Ts);
};

// ---------------------------------------------------------------- 反射：类型列表运算

/// 模板特化的实参（先去掉别名）。
[[nodiscard]] consteval infos arguments(info type)
{
	return ::std::meta::template_arguments_of(::std::meta::dealias(type));
}

[[nodiscard]] consteval info list(infos const& types)
{
	return ::std::meta::substitute(^^type_list, types);
}

[[nodiscard]] consteval bool same(info left, info right)
{
	return ::std::meta::dealias(left) == ::std::meta::dealias(right);
}

/// 不存在时等于 size。
[[nodiscard]] consteval ::std::size_t index_of(info type, infos const& types)
{
	for (::std::size_t index = 0; index < types.size(); ++index)
	{
		if (same(type, types[index]))
		{
			return index;
		}
	}
	return types.size();
}

[[nodiscard]] consteval bool contains(info type, infos const& types)
{
	return index_of(type, types) != types.size();
}

/// 保留首次出现的顺序去重。
[[nodiscard]] consteval infos unique(infos const& types)
{
	infos result;
	for (info type : types)
	{
		if (!contains(type, result))
		{
			result.push_back(type);
		}
	}
	return result;
}

[[nodiscard]] consteval infos concat(::std::vector<infos> const& lists)
{
	infos result;
	for (infos const& one : lists)
	{
		result.insert(result.end(), one.begin(), one.end());
	}
	return result;
}

/// 对每个类型套一层单参数模板（别名模板也可以）。
[[nodiscard]] consteval infos map(info function, infos const& types)
{
	infos result;
	for (info type : types)
	{
		result.push_back(::std::meta::dealias(::std::meta::substitute(function, {type})));
	}
	return result;
}

/// 0, 1, ..., count - 1
[[nodiscard]] consteval ::std::vector<::std::size_t> iota(::std::size_t count)
{
	::std::vector<::std::size_t> result;
	for (::std::size_t index = 0; index < count; ++index)
	{
		result.push_back(index);
	}
	return result;
}

/// std::index_sequence<values...>
[[nodiscard]] consteval info indices(::std::vector<::std::size_t> const& values)
{
	infos constants;
	for (::std::size_t value : values)
	{
		constants.push_back(::std::meta::reflect_constant(value));
	}
	return ::std::meta::substitute(^^::std::index_sequence, constants);
}

/// 把 consteval 里的 vector 固定成 std::array（长度要先用另一次 consteval 调用算出来）。
template<::std::size_t N, class T>
[[nodiscard]] consteval ::std::array<T, N> to_array(::std::vector<T> const& values)
{
	::std::array<T, N> result{};
	for (::std::size_t index = 0; index < N; ++index)
	{
		result[index] = values[index];
	}
	return result;
}

// ---------------------------------------------------------------- 把 info 拼回类型

template<::std::size_t I, class List>
using at_t = typename [: meta::arguments(^^List)[I] :];

/// 把 type_list 的元素喂给另一个模板。
template<template<class...> class To, class List>
using rebind_t = typename [: ::std::meta::substitute(^^To, meta::arguments(^^List)) :];

// ---------------------------------------------------------------- 按索引放置的元组（运行期容器）

/// 工厂构造标记：元素从 `factory()` 的纯右值直接构造（保证复制消除），
/// 所以可以装 operation state 这类不可移动的对象。
struct from_factory_t
{
	explicit from_factory_t() = default;
};
inline constexpr from_factory_t from_factory{};

template<::std::size_t I, class T>
struct leaf
{
	SDAG_NO_UNIQUE_ADDRESS T value;

	constexpr leaf() = default;

	template<class Factory>
	constexpr explicit leaf(from_factory_t, Factory&& factory)
		noexcept(noexcept(T(::std::forward<Factory>(factory)())))
		: value(::std::forward<Factory>(factory)())
	{
	}
};

/// 被优化掉的元素：没有任何成员，各个 leaf<I, empty> 类型互不相同，空基类优化让它们都是 0 字节。
template<::std::size_t I>
struct leaf<I, empty>
{
	constexpr leaf() = default;

	template<class Factory>
	constexpr explicit leaf(from_factory_t, Factory&&) noexcept
	{
	}
};

template<class Indices, class... Ts>
struct indexed_tuple_impl;

template<::std::size_t... Is, class... Ts>
struct SDAG_EMPTY_BASES indexed_tuple_impl<::std::index_sequence<Is...>, Ts...> : leaf<Is, Ts>...
{
	constexpr indexed_tuple_impl() = default;

	template<class... Factories>
		requires (sizeof...(Factories) == sizeof...(Ts))
	constexpr explicit indexed_tuple_impl(from_factory_t, Factories&&... factories)
		noexcept((noexcept(leaf<Is, Ts>(from_factory, ::std::forward<Factories>(factories))) && ...))
		: leaf<Is, Ts>(from_factory, ::std::forward<Factories>(factories))...
	{
	}
};

/// 每个元素独立占一个基类，空类型靠空基类优化消失；
/// 没有 std::tuple 那种"先构造再移动"的要求。
template<class... Ts>
struct indexed_tuple : indexed_tuple_impl<::std::index_sequence_for<Ts...>, Ts...>
{
	using indexed_tuple_impl<::std::index_sequence_for<Ts...>, Ts...>::indexed_tuple_impl;
};

template<class List>
using indexed_tuple_of = rebind_t<indexed_tuple, List>;

template<::std::size_t I, class T>
[[nodiscard]] constexpr T& get(leaf<I, T>& l) noexcept
{
	return l.value;
}

template<::std::size_t I, class T>
[[nodiscard]] constexpr T const& get(leaf<I, T> const& l) noexcept
{
	return l.value;
}

}
