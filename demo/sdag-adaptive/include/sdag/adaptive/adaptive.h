#pragma once

/// sdag-adaptive：自适配图——平时与 ::sdag::dag 一样融合成一个 op-state（0 开销）；所在 domain 定制了 when_all 时
/// （例如 GPU 的 stream domain），自动降解成 when_all / split / let_value，让 domain 拿到它认识的标准算法。
/// 依赖 sdag 核心与 sdag-compose；两者都不认识它。
///
///   ::sdag::adaptive::adaptive_dag(nodes...)    建一张自适配图
///   ::sdag::adaptive::adaptive_dag_t            它的算法标签（带 transform_sender）
///
/// 为什么是独立的标签：stdexec 在 connect 前调用"标签的 transform_sender"，核心的 dag_t 没有它，图总是融合；
/// 自适配要在建图时就选定——用 ::sdag::dag 建的图不会降解，用 adaptive_dag 建的才会。
///
///   domain_custom.h     某个 domain 在某个阶段会不会改写某个 sender
///   lowering_policy.h   这一阶段要不要降解（when_all 探针）
///   adaptive.h          本文件：标签与入口

#include <utility>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include <sdag/sdag.h>
#include <sdag/compose/lowering.h>

#include "lowering_policy.h"

namespace sdag::adaptive
{

struct adaptive_dag_t
{
	template<graph_node... Nodes>
	[[nodiscard]] constexpr auto operator()(Nodes&&... nodes) const
	{
		return ::sdag::make_graph<adaptive_dag_t>(::std::forward<Nodes>(nodes)...);
	}

	/// 所在 domain 会定制 when_all 时，把整图降解成 when_all / split / let_value 组合，让它吃到定制。
	/// 不会时（默认 domain、只定制 bulk 的线程池 domain 等）这个重载不参与，stdexec 用 graph_sender::connect 走融合 op-state。
	template<class OpTag, class Sender, class Env>
		requires (adaptive::lowering_wanted<OpTag, graph::graph_state, Sender&&, Env>())
	[[nodiscard]] static constexpr auto transform_sender(OpTag, Sender&& sender, Env const&)
	{
		return compose::lower(::std::forward<Sender>(sender), ::stdexec::when_all, ::exec::split);
	}
};

inline constexpr adaptive_dag_t adaptive_dag{};

}
