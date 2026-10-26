#pragma once

/// 节点所见环境：节点实际看到哪个环境（要不要自带停止令牌），以及各节点签名在其中是否都合法。
///
///   node_view<SourceOf, Self, Env>     一次给出：
///     env_type        节点实际看到的环境
///     stoppable       整图要不要自己的停止源
///     analysis / plan 在 env_type 下的节点完成分析与存储计划
///     valid           各节点签名在探测环境、实际环境下都合法
///     first_invalid_t 不合法时第一个不合法的结果（先看探测环境，再看实际环境）；合法时为 void
///   node_env_t / evaluable_in             node_view 的两个简写
///
/// SourceOf<Self, Env> 给出这张图在环境 Env 下的取值源类型（graph/slots.h 的整图状态）。传模板而不是类型，
/// 是因为取值源本身随外层环境变化，而属性、签名的查询要对每个环境分别回答。
///
/// 要不要停止源，取决于节点会不会失败；而节点的签名又取决于它看到的环境——鸡生蛋。
/// 解法：先在"不带停止令牌"的探测环境下算一遍。节点在探测环境下都不会 error / stopped / 存值抛异常，
/// 就不需要停止源（多出来的停止令牌也不会让一个本来不会失败的节点变得会失败）。不需要时两个环境是同一个类型。

#include <type_traits>

#include "../config.h"
#include "../protocol/node_env.h"
#include "analysis.h"
#include "storage_plan.h"

namespace sdag::graph
{

template<template<class, class> class SourceOf, class Self, class Env>
struct node_view
{
	using probe_env = protocol::node_env<SourceOf, Self, Env, false>;
	using probe_analysis = node_analysis<Self, probe_env>;

	/// 探测环境下就不合法时没有停止源可言，按不需要处理（之后也只用来报告 first_invalid_t）。
	static constexpr bool stoppable = probe_analysis::valid && storage_plan<Self, probe_env>::can_fail;

	using env_type = protocol::node_env<SourceOf, Self, Env, stoppable>;
	using analysis = node_analysis<Self, env_type>;
	using plan = storage_plan<Self, env_type>;

	static constexpr bool valid = probe_analysis::valid && analysis::valid;
	using first_invalid_t = ::std::conditional_t<probe_analysis::valid,
		typename analysis::first_invalid_t, typename probe_analysis::first_invalid_t>;
};

template<template<class, class> class SourceOf, class Self, class Env>
using node_env_t = typename node_view<SourceOf, Self, Env>::env_type;

template<template<class, class> class SourceOf, class Self, class Env>
[[nodiscard]] consteval bool evaluable_in() noexcept
{
	return node_view<SourceOf, Self, Env>::valid;
}

}
