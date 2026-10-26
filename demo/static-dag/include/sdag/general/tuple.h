#pragma once

/// 不可移动元组：按序存放、就地构造不可移动对象、空元素 0 字节。
///
///   empty, empty_for<Owner, N>  被优化掉的成员的占位类型
///   indexed_tuple<Ts...>        sdag 运行期唯一的容器，get<I>(t)

#include <cstddef>
#include <type_traits>
#include <utility>

#include "../config.h"

namespace sdag::general
{

/// 被优化掉的元素用它占位：indexed_tuple 里它是真正的 0 字节空基类。
struct empty
{
};

/// 一个类里有多个被优化掉的成员时，各用一个互不相同的空类型（Owner 是那个类，N 区分成员）：
/// 两个同类型的空成员必须有不同地址，no_unique_address 也压不掉其中一个。
template<class Owner, ::std::size_t N = 0>
struct empty_for
{
};

// ---------------------------------------------------------------- 按索引放置的元组（运行期容器）
//
// indexed_tuple<T...> 是 sdag 运行期唯一的容器：图 sender 存节点 sender、融合 op-state 存节点 op-state / 值槽 / 入度计数，都用它。
// 选它而不用 std::tuple，是因为两点：
//   1. 元素能从"工厂的返回值"直接构造（保证复制消除），所以能装不可移动的 operation state；
//      std::tuple 的构造函数总是先物化一个临时对象再移动进去。
//   2. 被优化掉的元素（general::empty）真正是 0 字节。
//
// 每个元素是一个基类 leaf<I, T>，按元素种类有三种写法：
//   leaf<I, empty>   被优化掉的元素：没有成员。各个 leaf<I, empty> 类型互不相同（I 不同），空基类优化让它们都不占空间。
//   leaf<I, 空类型>  用户的空类型（无状态的 lambda、空 data……）：成员加 no_unique_address，同样不占空间。
//   leaf<I, 非空>    clang 上也加 no_unique_address，让后面的元素复用它的尾部填充；GCC 上不加，原因见 config.h 的
//                    SDAG_OVERLAPPING_ELEMENT（GCC 对"可能重叠的子对象"不做复制消除，不可移动的 op-state 会编不过）。

/// 工厂构造标记：元素从 factory() 的纯右值直接构造。
struct from_factory_t
{
	explicit from_factory_t() = default;
};
inline constexpr from_factory_t from_factory{};

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

template<::std::size_t I>
struct leaf<I, empty>
{
	constexpr leaf() = default;

	/// 被优化掉的元素：工厂不调用。
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

template<class... Ts>
struct indexed_tuple : indexed_tuple_impl<::std::index_sequence_for<Ts...>, Ts...>
{
	using indexed_tuple_impl<::std::index_sequence_for<Ts...>, Ts...>::indexed_tuple_impl;
};

namespace tuple_detail
{

/// 按基类 leaf<I, T> 推导出 T。
template<::std::size_t I, class T>
T leaf_type(leaf<I, T> const&);

}

/// 第 I 个元素，值类别跟随 tuple。
template<::std::size_t I, class Tuple>
[[nodiscard]] constexpr auto&& get(Tuple&& tuple) noexcept
{
	using element = decltype(tuple_detail::leaf_type<I>(tuple));
	using base = ::std::conditional_t<::std::is_const_v<::std::remove_reference_t<Tuple>>, leaf<I, element> const, leaf<I, element>>;
	return ::std::forward_like<Tuple>(static_cast<base&>(tuple).value);
}

}
