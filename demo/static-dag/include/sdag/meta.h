#pragma once

/// 两种计算实现共享的基础设施：类型列表的壳、运行期容器 indexed_tuple，以及几个与计算方式无关的小工具。
/// 类型列表上的运算（查找、去重、拼接……）在 compute/ 下，按 SDAG_REFLECTION 分别实现。

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include "config.h"

namespace sdag::meta
{

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

template<::std::size_t I, class List>
struct at;

template<::std::size_t I, class... Ts>
struct at<I, type_list<Ts...>>
{
	using type = Ts...[I];
};

template<::std::size_t I, class List>
using at_t = typename at<I, List>::type;

template<class From, class To>
using copy_cvref_t = decltype(::std::forward_like<From>(::std::declval<To&>()));

/// 把编译期的 vector 固定成 std::array（长度要先用另一次常量求值算出来）。
template<::std::size_t N, class T>
[[nodiscard]] constexpr ::std::array<T, N> to_array(::std::vector<T> const& values)
{
	::std::array<T, N> result{};
	for (::std::size_t index = 0; index < N; ++index)
	{
		result[index] = values[index];
	}
	return result;
}

// ---------------------------------------------------------------- 按索引放置的元组（运行期容器）

/// 工厂构造标记：元素从 `factory()` 的纯右值直接构造（保证复制消除），
/// 所以可以装 operation state 这类不可移动的对象。
struct from_factory_t
{
	explicit from_factory_t() = default;
};
inline constexpr from_factory_t from_factory{};

/// 非空元素：clang 上加 no_unique_address 复用尾部填充，GCC 上不加（见 config.h 的 SDAG_OVERLAPPING_ELEMENT）。
template<::std::size_t I, class T>
struct leaf
{
	SDAG_OVERLAPPING_ELEMENT T value;

	constexpr leaf() = default;

	template<class Factory>
	constexpr explicit leaf(from_factory_t, Factory&& factory)
		noexcept(noexcept(T(::std::forward<Factory>(factory)())))
		: value(::std::forward<Factory>(factory)())
	{
	}
};

template<::std::size_t I, class T>
	requires (::std::is_empty_v<T> && !::std::is_same_v<T, empty>)
struct leaf<I, T>
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

template<::std::size_t I, class T>
[[nodiscard]] constexpr T&& get(leaf<I, T>&& l) noexcept
{
	return static_cast<T&&>(l.value);
}

template<::std::size_t I, class T>
[[nodiscard]] constexpr T const&& get(leaf<I, T> const&& l) noexcept
{
	return static_cast<T const&&>(l.value);
}

}
