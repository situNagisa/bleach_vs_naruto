#pragma once

/// 是否降解：当前 domain 会不会改写降解后的 when_all。
/// 依赖：sdag 核心（图的描述、有向无环图、前驱占位符、前驱的签名、编译期序列、节点所见环境）、sdag-compose 的换掉占位符、domain 改写。
///
/// stdexec 在 connect 之前会调用 transform_sender，分两个阶段问 domain：start_t 阶段问环境里的起始 domain，
/// set_value_t 阶段问 sender 自己报告的完成域。我们想知道的是："如果把这张图写成 when_all / split / let_value，
/// 这个 domain 会不会改写它"。会（例如 GPU 的 stream_domain），就降解，让 domain 拿到它认识的标准算法；
/// 不会（默认 domain、只定制 bulk 的线程池 domain），就保持融合，0 开销。
/// 直接问"会不会改写降解后的整棵树"代价太大，所以拿一个探针问：when_all(各汇点)，其中汇点里的占位符换成
/// 签名相同的占位 sender。domain 对 when_all 的定制看的正是这一层。
///
///   lowering_wanted<OpTag, SourceOf, Self, Env>()   这一阶段要不要降解（SourceOf 见 graph/visibility.h）

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include <sdag/config.h>
#include <sdag/general/sequence.h>
#include <sdag/general/digraph.h>
#include <sdag/protocol/hole.h>
#include <sdag/graph/description.h>
#include <sdag/graph/pred_sigs.h>
#include <sdag/graph/visibility.h>
#include <sdag/compose/hole_fill.h>

#include "domain_custom.h"

namespace sdag::adaptive
{

/// 探针里的占位 sender：完成签名与节点里真实的占位符在整图环境下相同，属性也相同。只用来算类型，不会被连接。
template<class Completions>
struct placeholder_sender
{
	using sender_concept = ::stdexec::sender_t;

	template<class Self, class... Env>
	[[nodiscard]] static consteval auto get_completion_signatures() noexcept
	{
		return Completions{};
	}

	[[nodiscard]] static constexpr protocol::hole_attrs get_env() noexcept
	{
		return {};
	}
};

/// 汇点 S 的 sender，占位符换成占位 sender：保留了汇点的属性（完成调度器等），又不需要整图环境就能算签名。
template<template<class, class> class SourceOf, class Self, class Env, size_t S>
using probe_sink_t = compose::hole_filled_t<graph::child_t<Self, S>,
	placeholder_sender<graph::pred_completions_t<Self, S, graph::node_env_t<SourceOf, Self, Env>>>>;

template<template<class, class> class SourceOf, class Self, class Env, class Sinks = general::sequence_t<general::sinks_v<graph::shape_of_v<Self>>>>
struct when_all_probe;

/// 探针 when_all(汇点...)。when_all 只有一个子 sender 时会原样返回它（不再是 when_all），所以只有一个汇点时重复一次。
template<template<class, class> class SourceOf, class Self, class Env, size_t... S>
struct when_all_probe<SourceOf, Self, Env, ::std::index_sequence<S...>>
{
	static consteval auto compute() noexcept
	{
		if constexpr (sizeof...(S) == 1)
		{
			return ::std::type_identity<decltype(::stdexec::when_all(::std::declval<probe_sink_t<SourceOf, Self, Env, S>>()...,
				::std::declval<probe_sink_t<SourceOf, Self, Env, S>>()...))>{};
		}
		else
		{
			return ::std::type_identity<decltype(::stdexec::when_all(::std::declval<probe_sink_t<SourceOf, Self, Env, S>>()...))>{};
		}
	}

	using type = typename decltype(compute())::type;
};

/// 这一阶段要不要把图降解成 when_all / split / let_value。
template<class OpTag, template<class, class> class SourceOf, class Self, class Env>
[[nodiscard]] consteval bool lowering_wanted() noexcept
{
	if constexpr (!graph::evaluable_in<SourceOf, Self, Env>())
	{
		return false;
	}
	else
	{
		using domain = typename decltype(adaptive::phase_domain<OpTag, Self, Env>())::type;
		if constexpr (::std::same_as<domain, ::stdexec::default_domain>)
		{
			return false;
		}
		else
		{
			return adaptive::customizes<domain, OpTag, typename when_all_probe<SourceOf, Self, Env>::type, Env>;
		}
	}
}

}
