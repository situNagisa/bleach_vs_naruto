#pragma once

/// 图的完成签名：在给定外层环境下，整张图的值 / 错误类型与完成签名（节点不合法时原样透传）。
///
/// 规则与 when_all 相同：
///   值完成      各汇点的值按声明顺序拼接；直接转发的汇点（唯一的汇点）用它的原始签名，不经槽位衰变
///   错误        各节点错误类型的并集；值存进 op-state 可能抛异常时再加 exception_ptr
///   停止        任一节点会停止时有 set_stopped
///   没有值完成  任一节点没有值完成时整图也没有（每个节点都是某个汇点的祖先）
///
///   graph_completions<Self, Env>          error_types / value_types / completions（在节点所见环境下算）
///   completions_of<SourceOf, Self, Env...>() 给 get_completion_signatures 用：无环境时报依赖型错误，
///                                         有不合法节点时把 stdexec 给出的错误类型原样返回（诊断指向真正出错的节点 sender）

#include <exception>
#include <type_traits>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/signatures.h"
#include <exec/completion_signatures.hpp>
#include "description.h"
#include "analysis.h"
#include "storage_plan.h"
#include "visibility.h"

namespace sdag::graph
{

namespace completions_detail
{

/// 汇点的值按声明顺序拼接；直接转发的汇点用它的原始值类型（不经槽位衰变）。
template<class Self, class Analysis, class Plan>
using value_types_t = decltype([]<size_t... I>(::std::index_sequence<I...>)
{
	return general::concat_t<::std::conditional_t<!Plan::shape.is_sink(I), general::type_list<>,
		::std::conditional_t<Plan::forward[I], typename Analysis::template raw_values_t<I>, typename Analysis::template values_t<I>>>...>{};
}(::std::make_index_sequence<count_of_v<Self>>{}));

template<class Self, class Analysis>
using error_types_t = decltype([]<size_t... I>(::std::index_sequence<I...>)
{
	return general::concat_t<typename Analysis::template errors_t<I>...>{};
}(::std::make_index_sequence<count_of_v<Self>>{}));

}

template<class Self, class Env>
struct graph_completions
{
	using analysis = node_analysis<Self, Env>;
	using plan = storage_plan<Self, Env>;

	using error_types = general::unique_t<general::concat_t<
		completions_detail::error_types_t<Self, analysis>,
		::std::conditional_t<plan::nothrow_store, general::type_list<>, general::type_list<::std::exception_ptr>>>>;

	using value_types = completions_detail::value_types_t<Self, analysis, plan>;

	using completions = general::make_completions_t<analysis::all_have_values, value_types, error_types, analysis::any_stopped>;
};

namespace completions_detail
{

template<template<class, class> class SourceOf, class Self, class Env>
[[nodiscard]] consteval auto with_env()
{
	using view = node_view<SourceOf, Self, Env>;
	if constexpr (!view::valid)
	{
		return typename view::first_invalid_t{};
	}
	else
	{
		return typename graph_completions<Self, typename view::env_type>::completions{};
	}
}

}

template<template<class, class> class SourceOf, class Self, class... Env>
[[nodiscard]] consteval auto completions_of()
{
	if constexpr (sizeof...(Env) == 0)
	{
		// 节点里的占位符依赖环境，所以整图在无环境下必然是 dependent sender。
		return ::exec::throw_compile_time_error<::stdexec::dependent_sender_error, ::stdexec::_WITH_SENDER_<::std::remove_cvref_t<Self>>>();
	}
	else
	{
		return completions_detail::with_env<SourceOf, Self, Env...>();
	}
}


}
