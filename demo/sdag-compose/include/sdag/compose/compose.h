#pragma once

/// sdag-compose：把 sdag 的图降解成现成的扇入 / 扇出算法拼成的普通 sender 树，扇入扇出可配置。
/// 依赖 sdag 核心；sdag 核心不认识它。
///
///   ::sdag::compose::lower(graph, fanin, fanout)                  把一张图（::sdag::dag(...) 的结果）降解
///   ::sdag::compose::composed<K>{fanin, fanout}(nodes...)         建图并立即降解（K 是键类型），默认 when_all + split
///
/// 降解不追求 0 开销：split 要堆分配共享状态。它的用途是语义参照、扇入扇出可配置，以及作为 sdag-adaptive 的底座。
///
///   expressions.h   重建 stdexec 的算法 sender（__sexpr）
///   hole_fill.h     把节点 sender 里的占位符换成输入 sender
///   lowering.h      降解本体与 lower
///   compose.h       本文件：composed

#include <utility>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include <sdag/sdag.h>

#include "lowering.h"

namespace sdag::compose
{

/// 建图并立即降解：composed<K>{fanin, fanout}(nodes...) = lower(::sdag::dag<K>(nodes...), fanin, fanout)。fanin 接收 1 个或多个 sender（只有 1 个时应原样返回），fanout 接收 1 个 sender 并返回可拷贝的 sender。
template<key_value K, class FanIn = ::stdexec::when_all_t, class FanOut = ::exec::split_t>
struct composed
{
	SDAG_NO_UNIQUE_ADDRESS FanIn _fanin{};
	SDAG_NO_UNIQUE_ADDRESS FanOut _fanout{};

	template<node_of<K>... Nodes>
	[[nodiscard]] constexpr auto operator()(Nodes&&... nodes) const
	{
		return compose::lower(::sdag::dag<K>(::std::forward<Nodes>(nodes)...), _fanin, _fanout);
	}
};

}
