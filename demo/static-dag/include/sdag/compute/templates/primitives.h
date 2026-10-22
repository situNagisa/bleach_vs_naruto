#pragma once

/// 编译期计算层（一）的模板实现：给还不支持静态反射的编译器（clang 22 / 23 等）用。
/// 编译器都支持反射之后，整个 templates/ 目录可以删除。

#include <array>
#include <cstddef>
#include <exception>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../../meta.h"
#include "../../fwd.h"
#include "../../stdexec_bridge.h"

namespace sdag::compute
{

inline namespace SDAG_ABI_NAMESPACE
{

// ---------------------------------------------------------------- 类型列表

template<class T, class List>
struct index_of;

template<class T, class... Ts>
struct index_of<T, meta::type_list<Ts...>>
{
	/// 不存在时等于 size。
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

template<class... Lists>
struct concat
{
	using type = meta::type_list<>;
};

template<class... Ts>
struct concat<meta::type_list<Ts...>>
{
	using type = meta::type_list<Ts...>;
};

template<class... Ts, class... Us, class... Rest>
struct concat<meta::type_list<Ts...>, meta::type_list<Us...>, Rest...>
{
	using type = typename concat<meta::type_list<Ts..., Us...>, Rest...>::type;
};

template<class... Lists>
using concat_t = typename concat<Lists...>::type;

template<class Accumulated, class Pending>
struct unique_impl;

template<class... Done>
struct unique_impl<meta::type_list<Done...>, meta::type_list<>>
{
	using type = meta::type_list<Done...>;
};

template<class... Done, class Next, class... Rest>
struct unique_impl<meta::type_list<Done...>, meta::type_list<Next, Rest...>>
{
	using type = typename unique_impl<
		::std::conditional_t<(::std::is_same_v<Next, Done> || ...), meta::type_list<Done...>, meta::type_list<Done..., Next>>,
		meta::type_list<Rest...>>::type;
};

/// 保留首次出现的顺序去重。
template<class List>
using unique_t = typename unique_impl<meta::type_list<>, List>::type;

template<template<class> class Fn, class List>
struct transform;

template<template<class> class Fn, class... Ts>
struct transform<Fn, meta::type_list<Ts...>>
{
	using type = meta::type_list<Fn<Ts>...>;
};

template<template<class> class Fn, class List>
using transform_t = typename transform<Fn, List>::type;

template<template<class...> class To, class List>
struct rebind;

template<template<class...> class To, class... Ts>
struct rebind<To, meta::type_list<Ts...>>
{
	using type = To<Ts...>;
};

template<template<class...> class To, class List>
using rebind_t = typename rebind<To, List>::type;

// ---------------------------------------------------------------- 常量数组

template<auto Array, class Indices = ::std::make_index_sequence<Array.size()>>
struct sequence_of;

template<auto Array, ::std::size_t... I>
struct sequence_of<Array, ::std::index_sequence<I...>>
{
	using type = ::std::index_sequence<Array[I]...>;
};

template<auto Array>
using sequence_t = typename sequence_of<Array>::type;

template<auto Array, class Fn>
constexpr void for_each_value(Fn&& fn)
{
	[&]<::std::size_t... I>(::std::index_sequence<I...>)
	{
		(fn.template operator()<Array[I]>(), ...);
	}(::std::make_index_sequence<Array.size()>{});
}

template<class Keys, class Dependency>
struct dependency_indices;

template<class Keys, template<class...> class After, class... Dependencies>
struct dependency_indices<Keys, After<Dependencies...>>
{
	static constexpr ::std::array<::std::size_t, sizeof...(Dependencies)> value{index_of_v<Dependencies, Keys>...};
};

template<class Keys, class Dependency>
inline constexpr auto dependency_indices_v = dependency_indices<Keys, Dependency>::value;

// ---------------------------------------------------------------- 完成签名

template<class T>
inline constexpr bool is_completion_signatures_v = false;

template<class... Signatures>
inline constexpr bool is_completion_signatures_v<::stdexec::completion_signatures<Signatures...>> = true;

template<class Signature>
struct classify;

template<class... Args>
struct classify<::stdexec::set_value_t(Args...)>
{
	using values = meta::type_list<meta::type_list<Args...>>;
	using errors = meta::type_list<>;
	static constexpr bool stopped = false;
};

template<class Error>
struct classify<::stdexec::set_error_t(Error)>
{
	using values = meta::type_list<>;
	using errors = meta::type_list<Error>;
	static constexpr bool stopped = false;
};

template<>
struct classify<::stdexec::set_stopped_t()>
{
	using values = meta::type_list<>;
	using errors = meta::type_list<>;
	static constexpr bool stopped = true;
};

template<class List>
inline constexpr bool all_nothrow_decay_copyable_v = false;

template<class... Ts>
inline constexpr bool all_nothrow_decay_copyable_v<meta::type_list<Ts...>> = (::std::is_nothrow_constructible_v<::std::decay_t<Ts>, Ts> && ...);

template<class Completions>
struct completion_summary;

/// 一组完成签名的摘要。每个节点至多一种 set_value 完成（when_all 的同一要求）。
template<class... Signatures>
struct completion_summary<::stdexec::completion_signatures<Signatures...>>
{
	using value_lists = concat_t<typename classify<Signatures>::values...>;
	static constexpr ::std::size_t value_count = value_lists::size;
	static_assert(value_count <= 1, "sdag: 每个节点至多只能有一种 set_value 完成；有多种时请先 ::stdexec::into_variant");

	/// 没有值完成的节点（例如 just_stopped()）永远不会带着值结束，它的后继也永远不会以值启动。
	static constexpr bool has_values = value_count == 1;
	using raw_values = meta::at_t<0, concat_t<value_lists, meta::type_list<meta::type_list<>>>>;
	using values = transform_t<::std::decay_t, raw_values>;
	using raw_errors = unique_t<concat_t<typename classify<Signatures>::errors...>>;
	using errors = transform_t<::std::decay_t, raw_errors>;
	static constexpr bool sends_stopped = (classify<Signatures>::stopped || ...);
	/// 存进 op-state 的拷贝 / 移动是否不抛。
	static constexpr bool nothrow_values = all_nothrow_decay_copyable_v<raw_values>;
	static constexpr bool nothrow_errors = all_nothrow_decay_copyable_v<raw_errors>;
};

template<bool HasValues, class Values, class Errors, bool SendsStopped>
struct make_completions;

template<class... Vs, class... Es, bool SendsStopped>
struct make_completions<true, meta::type_list<Vs...>, meta::type_list<Es...>, SendsStopped>
{
	using type = ::std::conditional_t<SendsStopped,
		::stdexec::completion_signatures<::stdexec::set_value_t(Vs...), ::stdexec::set_error_t(Es)..., ::stdexec::set_stopped_t()>,
		::stdexec::completion_signatures<::stdexec::set_value_t(Vs...), ::stdexec::set_error_t(Es)...>>;
};

template<class... Vs, class... Es, bool SendsStopped>
struct make_completions<false, meta::type_list<Vs...>, meta::type_list<Es...>, SendsStopped>
{
	using type = ::std::conditional_t<SendsStopped,
		::stdexec::completion_signatures<::stdexec::set_error_t(Es)..., ::stdexec::set_stopped_t()>,
		::stdexec::completion_signatures<::stdexec::set_error_t(Es)...>>;
};

template<bool HasValues, class Values, class Errors, bool SendsStopped>
using make_completions_t = typename make_completions<HasValues, Values, Errors, SendsStopped>::type;

template<class Values>
struct value_completions;

template<class... Vs>
struct value_completions<meta::type_list<Vs...>>
{
	using type = ::stdexec::completion_signatures<::stdexec::set_value_t(Vs...)>;
};

template<class Values>
using value_completions_t = typename value_completions<Values>::type;

// ---------------------------------------------------------------- sender 表达式

template<class T>
inline constexpr bool is_input_v = false;

template<class Dependency>
inline constexpr bool is_input_v<input_sender<Dependency>> = true;

template<class T>
inline constexpr bool is_graph_v = false;

template<class Data, class... Children>
inline constexpr bool is_graph_v<graph_sender<Data, Children...>> = true;

/// 输入叶子的个数（递归穿过 stdexec 的算法层；嵌套的图有自己的输入，不算在这里）。
template<class Sender>
[[nodiscard]] consteval ::std::size_t input_count()
{
	using plain = ::std::remove_cvref_t<Sender>;
	if constexpr (is_input_v<plain>)
	{
		return 1;
	}
	else if constexpr (is_graph_v<plain> || !bridge::sender_expression<plain>)
	{
		return 0;
	}
	else
	{
		return []<class Tag, class Data, class... Children>(meta::type_list<Tag, Data, Children...>)
		{
			return (input_count<Children>() + ... + ::std::size_t{0});
		}(bridge::parts_t<plain>{});
	}
}

template<class Sender>
inline constexpr ::std::size_t input_count_v = compute::input_count<Sender>();

template<class Sender, class Input>
struct substituted;

template<class Sender, class Input>
using substituted_t = typename substituted<Sender, Input>::type;

template<class Parts, class Input>
struct rebuilt;

/// 沿 sender 表达式层层重建：标签、data 不变，子 sender 递归替换。
template<class Tag, class Data, class... Children, class Input>
struct rebuilt<meta::type_list<Tag, Data, Children...>, Input>
{
	using type = bridge::expression_t<Tag, Data, substituted_t<Children, Input>...>;
};

template<class Sender, class Input>
struct substituted
{
	using plain = ::std::remove_cvref_t<Sender>;

	template<class T>
	struct identity
	{
		using type = T;
	};

	static consteval auto compute() noexcept
	{
		if constexpr (is_input_v<plain>)
		{
			return identity<::std::decay_t<Input>>{};
		}
		else if constexpr (input_count_v<plain> == 0)
		{
			return identity<plain>{};
		}
		else
		{
			return identity<typename rebuilt<bridge::parts_t<plain>, Input>::type>{};
		}
	}

	using type = typename decltype(compute())::type;
};

}

}
