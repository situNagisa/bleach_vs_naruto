#pragma once

/// 类型列表与"按索引放置"的小工具。整个库只在编译期处理图，这里是它用到的全部元编程原语。

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

/// MSVC ABI（clang-cl）下 [[no_unique_address]] 被忽略，要用 [[msvc::no_unique_address]]；
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

/// 被优化掉的成员用它占位。indexed_tuple 对它特化成真正的空基类，不占空间。
struct empty
{
};

/// 同一个类里多个被优化掉的成员要用互不相同的空类型，否则 no_unique_address 也得给它们分配不同地址。
template<class Tag>
struct empty_for
{
};

template<class T>
struct tag
{
	using type = T;
};

template<class... Ts>
struct type_list
{
	static constexpr ::std::size_t size = sizeof...(Ts);
};

// ---------------------------------------------------------------- 查找

template<class T, class List>
struct index_of;

template<class T, class... Ts>
struct index_of<T, type_list<Ts...>>
{
	/// 不存在时等于 size，调用方据此判断"未找到"。
	static constexpr ::std::size_t value = []
	{
		constexpr ::std::array<bool, sizeof...(Ts)> same{::std::is_same_v<T, Ts>...};
		for (::std::size_t index = 0; index < same.size(); ++index)
		{
			if (same[index])
			{
				return index;
			}
		}
		return sizeof...(Ts);
	}();
};

template<class T, class List>
inline constexpr ::std::size_t index_of_v = index_of<T, List>::value;

template<class T, class List>
inline constexpr bool contains_v = index_of_v<T, List> != List::size;

template<::std::size_t I, class List>
struct at;

template<::std::size_t I, class... Ts>
struct at<I, type_list<Ts...>>
{
	using type = Ts...[I];
};

template<::std::size_t I, class List>
using at_t = typename at<I, List>::type;

// ---------------------------------------------------------------- 拼接 / 去重 / 变换

template<class... Lists>
struct concat
{
	using type = type_list<>;
};

template<class... Ts>
struct concat<type_list<Ts...>>
{
	using type = type_list<Ts...>;
};

template<class... Ts, class... Us, class... Rest>
struct concat<type_list<Ts...>, type_list<Us...>, Rest...>
{
	using type = typename concat<type_list<Ts..., Us...>, Rest...>::type;
};

template<class... Lists>
using concat_t = typename concat<Lists...>::type;

template<class Accumulated, class Pending>
struct unique_impl;

template<class... Done>
struct unique_impl<type_list<Done...>, type_list<>>
{
	using type = type_list<Done...>;
};

template<class... Done, class Next, class... Rest>
struct unique_impl<type_list<Done...>, type_list<Next, Rest...>>
{
	using type = typename unique_impl<
		::std::conditional_t<contains_v<Next, type_list<Done...>>, type_list<Done...>, type_list<Done..., Next>>,
		type_list<Rest...>>::type;
};

/// 保留首次出现的顺序去重。
template<class List>
using unique_t = typename unique_impl<type_list<>, List>::type;

template<template<class> class Fn, class List>
struct transform;

template<template<class> class Fn, class... Ts>
struct transform<Fn, type_list<Ts...>>
{
	using type = type_list<Fn<Ts>...>;
};

template<template<class> class Fn, class List>
using transform_t = typename transform<Fn, List>::type;

/// 把 type_list 的元素喂给另一个模板。
template<template<class...> class To, class List>
struct rebind;

template<template<class...> class To, class... Ts>
struct rebind<To, type_list<Ts...>>
{
	using type = To<Ts...>;
};

template<template<class...> class To, class List>
using rebind_t = typename rebind<To, List>::type;

template<class From, class To>
using copy_cvref_t = decltype(::std::forward_like<From>(::std::declval<To&>()));

// ---------------------------------------------------------------- 按索引放置的元组

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

// ---------------------------------------------------------------- 索引序列工具

/// 从 constexpr 谓词筛出 [0, N) 中满足条件的索引。
template<::std::size_t N, auto Predicate>
constexpr auto filtered_indices = []
{
	constexpr ::std::size_t count = []
	{
		::std::size_t total = 0;
		for (::std::size_t index = 0; index < N; ++index)
		{
			total += Predicate(index) ? 1 : 0;
		}
		return total;
	}();
	::std::array<::std::size_t, count> result{};
	::std::size_t cursor = 0;
	for (::std::size_t index = 0; index < N; ++index)
	{
		if (Predicate(index))
		{
			result[cursor++] = index;
		}
	}
	return result;
}();

template<auto Array>
struct index_sequence_of_impl
{
	template<::std::size_t... Is>
	static ::std::index_sequence<Array[Is]...> make(::std::index_sequence<Is...>);

	using type = decltype(make(::std::make_index_sequence<Array.size()>{}));
};

/// 把 constexpr 的 std::array<size_t, N> 变成 std::index_sequence。
template<auto Array>
using index_sequence_of = typename index_sequence_of_impl<Array>::type;

}
