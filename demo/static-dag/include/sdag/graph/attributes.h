#pragma once

/// 图的属性：图的完成域与完成行为（值完成在汇点，失败可能来自任一节点）。
///
///   graph_attributes<SourceOf, Self>   图 sender 的 get_env() 返回它（SourceOf 见 graph/visibility.h）
///     get_completion_domain<set_value_t>     各汇点在节点所见环境下的公共完成域
///     get_completion_domain<set_error_t / set_stopped_t>   汇点与全部节点的公共域
///     get_completion_behavior<Tag>           全部节点的合成（空图是 inline_completion）
///   节点签名在某个环境下不合法时，这些查询安静地不回答，把诊断留给 connect。

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/completion_behavior.hpp>

#include "../config.h"
#include "../general/type_list.h"
#include "analysis.h"
#include "../general/sequence.h"
#include "../general/digraph.h"
#include "../general/domain_merge.h"
#include "description.h"
#include "node_domain.h"
#include "visibility.h"

namespace sdag::graph
{

template<template<class, class> class SourceOf, class Self>
struct graph_attributes
{
	static constexpr auto sink_list = general::to_array<shape_of<Self>().sink_count()>(shape_of<Self>().sinks());

	template<class Indices = general::sequence_t<sink_list>>
	struct sinks;

	template<size_t... S>
	struct sinks<::std::index_sequence<S...>>
	{
		using type = general::type_list<child_t<Self, S>...>;
	};

	using sink_children = typename sinks<>::type;

	/// 汇点在节点环境下的公共值完成域；有汇点给不出时为 void。
	template<class Env>
	using value_domain_t = common_completion_domain_t<node_env_t<SourceOf, Self, Env>, sink_children>;

	/// 全部节点的公共值完成域；有节点给不出时为 void。
	template<class Env>
	using any_node_domain_t = common_completion_domain_t<node_env_t<SourceOf, Self, Env>, typename description_of<Self>::children>;

	template<class Env>
	using failure_domain_t = general::common_domain_t<general::type_list<value_domain_t<Env>, any_node_domain_t<Env>>>;

	template<class Env>
		requires (evaluable_in<SourceOf, Self, Env>()) && (!::std::is_void_v<value_domain_t<Env>>)
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_value_t>, Env const&) const noexcept
		-> value_domain_t<Env>
	{
		return {};
	}

	/// 失败（set_error / set_stopped）可能来自任一节点。
	template<class Tag, class Env>
		requires (::std::same_as<Tag, ::stdexec::set_error_t> || ::std::same_as<Tag, ::stdexec::set_stopped_t>)
			&& (evaluable_in<SourceOf, Self, Env>()) && (!::std::is_void_v<value_domain_t<Env>>) && (!::std::is_void_v<any_node_domain_t<Env>>)
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<Tag>, Env const&) const noexcept
		-> failure_domain_t<Env>
	{
		return {};
	}

	/// 子 sender 的完成行为：直接调公开的查询对象，结果是类型化的常量（exec::completion_behavior::*_t）。
	template<class Tag, class Child, class... Env>
	using child_behavior_t = decltype(::exec::get_completion_behavior_t<Tag>{}(
		::stdexec::get_env(::std::declval<Child const&>()), ::std::declval<Env const&>()...));

	template<class Tag, class... Env>
	[[nodiscard]] constexpr auto query(::exec::get_completion_behavior_t<Tag>, Env const&...) const noexcept
	{
		if constexpr (count_of_v<Self> == 0)
		{
			// 空图：start 里立即 set_value()。
			return ::exec::completion_behavior::inline_completion;
		}
		else
		{
			return [&]<size_t... I>(::std::index_sequence<I...>)
			{
				return ::exec::completion_behavior::common(child_behavior_t<Tag, child_t<Self, I>, Env...>{}...);
			}(::std::make_index_sequence<count_of_v<Self>>{});
		}
	}
};

}
