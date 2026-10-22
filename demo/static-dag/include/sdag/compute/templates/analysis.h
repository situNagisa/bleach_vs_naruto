#pragma once

/// 编译期计算层（二）的模板实现：给还不支持静态反射的编译器用。编译器都支持反射之后，整个 templates/ 目录可以删除。

#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../../meta.h"
#include "../../graph_sender.h"
#include "primitives.h"

namespace sdag::compute
{

inline namespace SDAG_ABI_NAMESPACE
{

namespace tmpl
{

using ::std::size_t;
using graph_detail::node_completions_t;

template<class T>
struct identity
{
	using type = T;
};

// ---------------------------------------------------------------- 合法性

template<class Self, class Indices, class Env>
struct validity;

template<class Self, size_t... I, class Env>
struct validity<Self, ::std::index_sequence<I...>, Env>
{
	static constexpr bool value = (is_completion_signatures_v<node_completions_t<Self, I, Env>> && ...);

	/// 第一个不合法的结果（按 Indices 的顺序）。
	template<size_t Cursor = 0>
	static consteval auto first_invalid()
	{
		constexpr ::std::array<size_t, sizeof...(I)> indices{I...};
		if constexpr (Cursor == sizeof...(I))
		{
			return identity<void>{};
		}
		else if constexpr (!is_completion_signatures_v<node_completions_t<Self, indices[Cursor], Env>>)
		{
			return identity<node_completions_t<Self, indices[Cursor], Env>>{};
		}
		else
		{
			return first_invalid<Cursor + 1>();
		}
	}

	using first_invalid_t = typename decltype(first_invalid())::type;
};

/// 节点 sender 的完成签名要先算前驱的：then(f) 问子 sender 的签名时，节点 N 的计算会嵌套进节点 N-1 的……
/// 长链会撞上编译器的模板深度上限。这里按拓扑序逐个实例化（基类先于派生类），让后面的计算全部命中
/// 编译器已有的实例化结果，嵌套深度变成常数。
template<class Self, size_t Step, class Env>
struct warm_up : warm_up<Self, Step - 1, Env>
{
	static constexpr size_t index = graph_detail::shape_of_v<Self>.order[Step - 1];
	static constexpr bool done = warm_up<Self, Step - 1, Env>::done && sizeof(identity<node_completions_t<Self, index, Env>>) > 0;
};

template<class Self, class Env>
struct warm_up<Self, 0, Env>
{
	static constexpr bool done = true;
};

template<class Self, class Env>
inline constexpr bool warmed_up = warm_up<Self, graph_detail::count_of_v<Self>, Env>::done;

// ---------------------------------------------------------------- 整图分析

template<class Self, bool Direct, size_t Sink, class Fallback, class Env>
struct graph_value_types
{
	using type = Fallback;
};

/// 唯一汇点时整图的值签名就是它的原始签名；延迟到确定 Direct 之后再取下标。
template<class Self, size_t Sink, class Fallback, class Env>
struct graph_value_types<Self, true, Sink, Fallback, Env>
{
	using type = typename completion_summary<node_completions_t<Self, Sink, Env>>::raw_values;
};

template<class Self, class Env, class Indices>
struct valid_analysis;

template<class Self, class Env, size_t... I>
struct valid_analysis<Self, Env, ::std::index_sequence<I...>>
{
	static constexpr auto shape = graph_detail::shape_of_v<Self>;
	static constexpr auto consumers = graph_detail::consumer_counts_v<Self>;

	template<size_t J>
	using node = completion_summary<node_completions_t<Self, J, Env>>;

	static constexpr bool valid = true;
	using first_invalid_t = void;

	template<size_t J>
	using values_t = typename node<J>::values;

	static constexpr bool direct_sink = shape.sink_count() == 1;
	static constexpr size_t sink_index = direct_sink ? sinks_v<shape>[0] : size_t{0};

	static constexpr ::std::array<bool, sizeof...(I)> slot_needed{
		(values_t<I>::size != 0 && ((shape.is_sink(I) && !direct_sink) || consumers[I] != 0))...};

	static constexpr bool sends_stopped = (node<I>::sends_stopped || ...);
	static constexpr bool has_errors = ((node<I>::errors::size != 0) || ...);
	static constexpr bool nothrow_store = ((node<I>::nothrow_errors && (!slot_needed[I] || node<I>::nothrow_values)) && ...);
	static constexpr bool can_fail = has_errors || sends_stopped || !nothrow_store;
	static constexpr bool has_values = (node<I>::has_values && ...);

	using error_types = unique_t<concat_t<
		typename node<I>::errors...,
		::std::conditional_t<nothrow_store, meta::type_list<>, meta::type_list<::std::exception_ptr>>>>;
	using value_types = typename graph_value_types<Self, direct_sink, sink_index,
		concat_t<::std::conditional_t<shape.is_sink(I), values_t<I>, meta::type_list<>>...>, Env>::type;
	using completions = make_completions_t<has_values, value_types, error_types, sends_stopped>;
};

template<class Self, class Env>
struct invalid_analysis
{
	static constexpr bool valid = false;
	using first_invalid_t = typename validity<Self, compute::sequence_t<graph_detail::shape_of_v<Self>.order>, Env>::first_invalid_t;
	static constexpr ::std::array<bool, graph_detail::count_of_v<Self>> slot_needed{};
	static constexpr bool direct_sink = false;
	static constexpr size_t sink_index = 0;
	static constexpr bool sends_stopped = false;
	static constexpr bool has_errors = false;
	static constexpr bool nothrow_store = true;
	static constexpr bool can_fail = false;
	static constexpr bool has_values = false;

	template<size_t J>
	using values_t = meta::type_list<>;

	using error_types = meta::type_list<>;
	using completions = void;
};

template<class Self, class Env>
inline constexpr bool all_valid = warmed_up<Self, Env>
	&& validity<Self, ::std::make_index_sequence<graph_detail::count_of_v<Self>>, Env>::value;

// ---------------------------------------------------------------- 输入 sender 的完成签名

template<bool Shared>
struct accessor
{
	/// 共享的值按 const& 交给后继；独占的值 move 过去（签名写成按值）。
	template<class V>
	using fn = ::std::conditional_t<Shared, V const&, V>;
};

template<class Self, class Env, class Indices>
struct input_completions;

/// 只看前驱节点的完成签名，不触发整图分析——否则会沿着"节点 -> 输入 sender -> 整图"递归实例化。
template<class Self, class Env, size_t... D>
struct input_completions<Self, Env, ::std::index_sequence<D...>>
{
	static_assert(((D < graph_detail::count_of_v<Self>) && ...), "sdag: 输入 sender 引用了图里不存在的键");

	template<size_t J>
	using node = completion_summary<node_completions_t<Self, J, Env>>;

	static consteval auto compute() noexcept
	{
		using checks = validity<Self, ::std::index_sequence<D...>, Env>;
		if constexpr (!checks::value)
		{
			return identity<typename checks::first_invalid_t>{};
		}
		else if constexpr (!(node<D>::has_values && ...))
		{
			return identity<::stdexec::completion_signatures<>>{};
		}
		else
		{
			return identity<value_completions_t<concat_t<
				transform_t<accessor<graph_detail::shared_v<Self, D>>::template fn, typename node<D>::values>...>>>{};
		}
	}

	using type = typename decltype(compute())::type;
};

// ---------------------------------------------------------------- 降解后的类型

template<class Self, class FanIn, class FanOut>
struct lowered
{
	static constexpr auto shape = graph_detail::shape_of_v<Self>;

	template<size_t I>
	struct node_sender;

	template<size_t I>
	using node_sender_t = typename node_sender<I>::type;

	/// 出度 > 1 的节点存的是扇出后的 sender。
	template<size_t I>
	using built_t = ::std::conditional_t<(shape.out_degree[I] > 1),
		::std::invoke_result_t<FanOut const&, node_sender_t<I>>,
		node_sender_t<I>>;

	/// 后继取前驱的方式：共享的拷贝一份扇出 sender，独占的直接 move。
	template<size_t D>
	using dependency_input_t = ::std::conditional_t<(shape.out_degree[D] > 1), built_t<D> const&, built_t<D>&&>;

	template<size_t I, class Dependencies>
	struct input;

	template<size_t I>
	struct input<I, ::std::index_sequence<>>
	{
		using type = decltype(::stdexec::just());
	};

	template<size_t I, size_t... D>
		requires (sizeof...(D) > 0)
	struct input<I, ::std::index_sequence<D...>>
	{
		using type = ::std::invoke_result_t<FanIn const&, dependency_input_t<D>...>;
	};

	template<size_t I>
	using input_t = typename input<I, graph_detail::dependencies_t<Self, I>>::type;

	template<size_t I>
	struct node_sender
	{
		using child = graph_detail::node_sender_t<Self, I>;

		static consteval auto compute() noexcept
		{
			if constexpr (input_count_v<child> != 0)
			{
				return identity<substituted_t<child, input_t<I>>>{};
			}
			else if constexpr (shape.is_root(I))
			{
				return identity<::std::remove_cvref_t<child>>{};
			}
			else
			{
				return identity<::std::invoke_result_t<::stdexec::let_value_t const&, input_t<I>,
					graph_detail::constant_sender<::std::remove_cvref_t<child>>>>{};
			}
		}

		using type = typename decltype(compute())::type;
	};

	template<class Indices>
	struct storage;

	template<size_t... I>
	struct storage<::std::index_sequence<I...>>
	{
		using type = ::std::tuple<::std::optional<built_t<I>>...>;
	};

	using storage_t = typename storage<::std::make_index_sequence<shape.count>>::type;
};

// ---------------------------------------------------------------- 公共完成域

template<class Domains>
struct common_domain;

/// 一组 domain 的公共 domain（与 stdexec 的规则一致）：都相同就是它；否则取 common_type；
/// 再否则带上 default_domain 取 common_type；都不行就是 indeterminate_domain（其中已有 indeterminate_domain<> 时就是它）。
template<class... Domains>
struct common_domain<meta::type_list<Domains...>>
{
	static consteval auto compute() noexcept
	{
		if constexpr (sizeof...(Domains) == 1)
		{
			return identity<Domains...[0]>{};
		}
		else if constexpr (requires { typename ::std::common_type_t<Domains...>; })
		{
			return identity<::std::common_type_t<Domains...>>{};
		}
		else if constexpr (requires { typename ::std::common_type_t<::stdexec::default_domain, Domains...>; })
		{
			return identity<::std::common_type_t<::stdexec::default_domain, Domains...>>{};
		}
		else if constexpr ((::std::is_same_v<Domains, ::stdexec::indeterminate_domain<>> || ...))
		{
			// 已经有一个"无法确定"的 domain，结果也就无法确定。
			return identity<::stdexec::indeterminate_domain<>>{};
		}
		else
		{
			return identity<::stdexec::indeterminate_domain<Domains...>>{};
		}
	}

	using type = typename decltype(compute())::type;
};

template<class Node, class Env>
concept sends_value = requires { typename ::stdexec::completion_signatures_of_t<Node, Env>; }
	&& completion_summary<::stdexec::completion_signatures_of_t<Node, Env>>::has_values
	&& requires { typename graph_detail::node_domain_t<Node, Env>; };

template<class Env, class Nodes>
struct common_completion_domain
{
	using type = void;
};

template<class Env, class... Nodes>
	requires (sends_value<Nodes, Env> && ...)
struct common_completion_domain<Env, meta::type_list<Nodes...>>
{
	using type = typename common_domain<unique_t<meta::type_list<graph_detail::node_domain_t<Nodes, Env>...>>>::type;
};

}

// ---------------------------------------------------------------- 对外接口

template<class Self, class Env>
struct analysis : ::std::conditional_t<tmpl::all_valid<Self, Env>,
	tmpl::valid_analysis<Self, Env, ::std::make_index_sequence<graph_detail::count_of_v<Self>>>,
	tmpl::invalid_analysis<Self, Env>>
{
};

template<class Self, class Dependency, class Env>
using input_completions_t = typename tmpl::input_completions<Self, Env,
	compute::sequence_t<graph_detail::dependency_indices_v<Self, Dependency>>>::type;

template<class Self, class FanIn, class FanOut>
using lowered = tmpl::lowered<Self, FanIn, FanOut>;

template<class Domains>
using common_domain_t = typename tmpl::common_domain<unique_t<Domains>>::type;

template<class Env, class Nodes>
using common_completion_domain_t = typename tmpl::common_completion_domain<Env, Nodes>::type;

}

}
