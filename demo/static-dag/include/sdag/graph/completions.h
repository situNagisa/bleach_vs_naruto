#pragma once

/// 图的完成签名：在给定外层环境下，整张图的值 / 错误类型与完成签名（节点不合法时原样透传）。
/// 依赖：节点完成分析、依赖型报错、图的描述、完成签名、存储计划、类型列表、节点所见环境。
///
/// 规则与 when_all 相同：
///   值完成      各汇点的值按声明顺序拼接；只有一个汇点时就是它的原始签名（值直接转发，不经槽位衰变）
///   错误        各节点错误类型的并集；值存进 op-state 可能抛异常时再加 exception_ptr
///   停止        任一节点会停止时有 set_stopped
///   没有值完成  任一节点没有值完成时整图也没有（每个节点都是某个汇点的祖先）
///
///   graph_completions<Self, Env>          valid / first_invalid_t / error_types / completions（在节点所见环境下算）
///   completions_of<SourceOf, Self, Env...>() 给 get_completion_signatures 用：无环境时报依赖型错误，
///                                         有不合法节点时把 stdexec 给出的错误类型原样返回（诊断指向真正出错的节点 sender）

#include <exception>
#include <type_traits>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/signatures.h"
#include "../stdexec/dependent.h"
#include "description.h"
#include "analysis.h"
#include "storage_plan.h"
#include "visibility.h"

namespace sdag::graph
{

namespace completions_detail
{

template<class Self, class Analysis, class Indices = ::std::make_index_sequence<count_of_v<Self>>>
struct value_types;

/// 汇点的值按声明顺序拼接；唯一汇点直接转发，用它的原始值类型。
template<class Self, class Analysis, size_t... I>
struct value_types<Self, Analysis, ::std::index_sequence<I...>>
{
	static constexpr auto shape = shape_of_v<Self>;
	static constexpr bool direct = shape.sink_count() == 1;

	static consteval auto compute() noexcept
	{
		if constexpr (direct)
		{
			return ::std::type_identity<typename Analysis::template raw_values_t<general::sinks_v<shape>[0]>>{};
		}
		else
		{
			return ::std::type_identity<general::concat_t<
				::std::conditional_t<shape.is_sink(I), typename Analysis::template values_t<I>, general::type_list<>>...>>{};
		}
	}

	using type = typename decltype(compute())::type;
};

template<class Self, class Analysis, class Indices = ::std::make_index_sequence<count_of_v<Self>>>
struct error_types;

template<class Self, class Analysis, size_t... I>
struct error_types<Self, Analysis, ::std::index_sequence<I...>>
{
	using type = general::concat_t<typename Analysis::template errors_t<I>...>;
};

}

template<class Self, class Env>
struct graph_completions
{
	using analysis = node_analysis<Self, Env>;
	using plan = storage_plan<Self, Env>;

	static constexpr bool valid = analysis::valid;
	using first_invalid_t = typename analysis::first_invalid_t;

	using error_types = general::unique_t<general::concat_t<
		typename completions_detail::error_types<Self, analysis>::type,
		::std::conditional_t<plan::nothrow_store, general::type_list<>, general::type_list<::std::exception_ptr>>>>;

	using value_types = typename completions_detail::value_types<Self, analysis>::type;

	using completions = general::make_completions_t<analysis::all_have_values, value_types, error_types, analysis::any_stopped>;
};

namespace completions_detail
{

template<template<class, class> class SourceOf, class Self, class Env>
[[nodiscard]] consteval auto with_env()
{
	using probe = node_analysis<Self, probe_env_t<SourceOf, Self, Env>>;
	if constexpr (!probe::valid)
	{
		return typename probe::first_invalid_t{};
	}
	else
	{
		using final = graph_completions<Self, node_env_t<SourceOf, Self, Env>>;
		if constexpr (!final::valid)
		{
			return typename final::first_invalid_t{};
		}
		else
		{
			return typename final::completions{};
		}
	}
}

}

template<template<class, class> class SourceOf, class Self, class... Env>
[[nodiscard]] consteval auto completions_of()
{
	if constexpr (sizeof...(Env) == 0)
	{
		// 节点里的占位符依赖环境，所以整图在无环境下必然是 dependent sender。
		return stdexec_adapter::dependent_sender_error<::std::remove_cvref_t<Self>>();
	}
	else
	{
		return completions_detail::with_env<SourceOf, Self, Env...>();
	}
}


}
