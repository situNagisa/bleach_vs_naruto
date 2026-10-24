#pragma once

/// 节点所见环境：节点实际看到哪个环境（要不要自带停止令牌），以及各节点签名在其中是否都合法。
/// 依赖：节点完成分析、节点运行环境、存储计划、取值源协议。
///
///   probe_env_t<SourceOf, Self, Env>      不带停止令牌的探测环境
///   stoppable_v<SourceOf, Self, Env>      整图要不要自己的停止源
///   node_env_t<SourceOf, Self, Env>       节点实际看到的环境
///   evaluable_in<SourceOf, Self, Env>()   各节点签名在探测环境、实际环境下都合法
///
/// SourceOf<Self, Env> 给出这张图在环境 Env 下的取值源类型（graph/slots.h 的整图状态）。传模板而不是类型，
/// 是因为取值源本身随外层环境变化，而属性、签名的查询要对每个环境分别回答。
///
/// 要不要停止源，取决于节点会不会失败；而节点的签名又取决于它看到的环境——鸡生蛋。
/// 解法：先在"不带停止令牌"的探测环境下算一遍。节点在探测环境下都不会 error / stopped / 存值抛异常，
/// 就不需要停止源（多出来的停止令牌也不会让一个本来不会失败的节点变得会失败）。
///
/// 节点的签名只经由环境里取值源的签名接口依赖 Source，所以 Source 只需要是一个提供签名接口的类型，不需要完整。

#include "../config.h"
#include "../protocol/node_env.h"
#include "analysis.h"
#include "storage_plan.h"

namespace sdag::graph
{

template<template<class, class> class SourceOf, class Self, class Env>
using probe_env_t = protocol::node_env<SourceOf, Self, Env, false>;

template<template<class, class> class SourceOf, class Self, class Env>
inline constexpr bool stoppable_v = storage_plan<Self, probe_env_t<SourceOf, Self, Env>>::can_fail;

template<template<class, class> class SourceOf, class Self, class Env>
using node_env_t = protocol::node_env<SourceOf, Self, Env, stoppable_v<SourceOf, Self, Env>>;

template<template<class, class> class SourceOf, class Self, class Env>
[[nodiscard]] consteval bool evaluable_in() noexcept
{
	if constexpr (!node_analysis<Self, probe_env_t<SourceOf, Self, Env>>::valid)
	{
		return false;
	}
	else
	{
		return node_analysis<Self, node_env_t<SourceOf, Self, Env>>::valid;
	}
}

}
