#pragma once

/// 公开入口：dag / fused / composed；定义算法标签 dag_t 及其 transform_sender。
/// 依赖：图的描述、图 sender、降解、是否降解、节点、节点集合成图、节点变 sender、不可移动元组、类型列表。
///
///   dag(nodes...)                                     默认：融合 op-state，0 开销
///   dag(fused{}, nodes...)                            同上
///   dag(composed{when_all, split}, nodes...)          无条件降解成 fanin / fanout / let_value 组合
///   dag_t                                             图算法的标签；dag_t{}(nodes...) 构造图 sender
///   dag_sender<Policy, Nodes...>                      纯类型层面的写法
///   shape_v<Graph>                                    图 sender 的形状
///
/// 默认路径也会在需要时自动降解：所在 domain 会定制 when_all 时（见 execution/lowering_policy.h），
/// dag_t::transform_sender 把图改写成标准算法交给 domain。

#include <concepts>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/tuple.h"
#include "../node/node.h"
#include "../node/node_graph.h"
#include "../protocol/node_sender.h"
#include "../graph/description.h"
#include "../graph/slots.h"
#include "../execution/lowering.h"
#include "../execution/lowering_policy.h"
#include "graph_sender.h"

namespace sdag
{

struct dag_t
{
	template<graph_node... Nodes>
	[[nodiscard]] constexpr auto operator()(Nodes&&... nodes) const
	{
		using data = graph::graph_data<general::type_list<node_key_t<Nodes>...>, general::type_list<node_dependencies_t<Nodes>...>>;
		using children = general::indexed_tuple<protocol::node_sender_of_t<Nodes>...>;
		using sender = graph_sender<dag_t, data, protocol::node_sender_of_t<Nodes>...>;
		// stdexec 靠结构化绑定识别标签；编译器缺少结构化绑定内省时这里会失效，自动降解也就静默关闭，所以显式检查。
		static_assert(::std::same_as<::stdexec::tag_of_t<sender>, dag_t>, "sdag: stdexec 认不出图 sender 的标签（编译器缺少结构化绑定内省）");
		return sender{{data{}, children(general::from_factory, [&] { return protocol::make_node_sender(::std::forward<Nodes>(nodes)); }...)}};
	}

	/// 所在 domain 会定制 when_all 时，把整图降解成 when_all / split / let_value 组合，让它吃到定制。
	/// 不会时（默认 domain、只定制 bulk 的线程池 domain 等）这个重载不参与，stdexec 用 graph_sender::connect 走融合 op-state。
	template<class OpTag, class Sender, class Env>
		requires (execution::lowering_wanted<OpTag, graph::graph_state, Sender&&, Env>())
	[[nodiscard]] static constexpr auto transform_sender(OpTag, Sender&& sender, Env const&)
	{
		return execution::lower(::std::forward<Sender>(sender), ::stdexec::when_all, ::exec::split);
	}
};

/// 图 sender 的形状（邻接矩阵、入出度、拓扑序）。
template<class Graph>
constexpr auto shape_v = graph::shape_of_v<Graph&&>;

// ---------------------------------------------------------------- 策略

/// 默认策略：图 sender 本身，连接时融合成一个 op-state。
struct fused
{
	template<graph_node... Nodes>
	[[nodiscard]] static constexpr auto compose(Nodes&&... nodes)
	{
		return dag_t{}(::std::forward<Nodes>(nodes)...);
	}
};

/// 组合策略：无条件降解。fanin 接收 1 个或多个 sender（只有 1 个时应原样返回），fanout 接收 1 个 sender 并返回可拷贝的 sender。
template<class FanIn = ::stdexec::when_all_t, class FanOut = ::exec::split_t>
struct composed
{
	SDAG_NO_UNIQUE_ADDRESS FanIn _fanin{};
	SDAG_NO_UNIQUE_ADDRESS FanOut _fanout{};

	template<graph_node... Nodes>
	[[nodiscard]] constexpr auto compose(Nodes&&... nodes) const
	{
		return execution::lower(dag_t{}(::std::forward<Nodes>(nodes)...), _fanin, _fanout);
	}
};

/// 策略：把一组节点组合成一个 sender。
template<class Policy, class... Nodes>
concept graph_policy_for = (graph_node<Nodes> && ...)
	&& requires(Policy const& policy, Nodes&&... nodes) {
		{ policy.compose(::std::forward<Nodes>(nodes)...) } -> ::stdexec::sender;
	};

template<class Policy, graph_node... Nodes>
	requires graph_policy_for<Policy, Nodes...>
[[nodiscard]] constexpr auto dag(Policy const& policy, Nodes&&... nodes)
	noexcept(noexcept(policy.compose(::std::forward<Nodes>(nodes)...)))
{
	return policy.compose(::std::forward<Nodes>(nodes)...);
}

template<graph_node... Nodes>
[[nodiscard]] constexpr auto dag(Nodes&&... nodes)
	noexcept(noexcept(fused::compose(::std::forward<Nodes>(nodes)...)))
{
	return fused::compose(::std::forward<Nodes>(nodes)...);
}

/// 手动降解：lower(graph, fanin, fanout)，与 composed 策略同一条路。
using execution::lower;

/// 纯类型层面的写法：static_assert(::stdexec::sender<dag_sender<fused, node<...>, ...>>)。
template<class Policy, class... Nodes>
	requires graph_policy_for<Policy, Nodes...>
using dag_sender = decltype(::std::declval<Policy const&>().compose(::std::declval<Nodes>()...));

}
