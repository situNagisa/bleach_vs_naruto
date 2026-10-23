#pragma once

/// 入口：dag_t（图算法的标签与构造）、两种策略、dag()。
///
///   dag(nodes...)                                          默认：融合 op-state，0 开销
///   dag(fused{}, nodes...)                                 同上
///   dag(composed{when_all, split}, nodes...)               无条件降解成 fanin / fanout / let_value 组合
///
/// 默认路径也会在需要时自动降解：所在 domain 会定制 when_all 时（见 attributes.h 的降解判据），
/// dag_t::transform_sender 把图改写成标准算法交给 domain。

#include <concepts>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "meta.h"
#include "fwd.h"
#include "node.h"
#include "graph_traits.h"
#include "graph_sender.h"
#include "node_input.h"
#include "attributes.h"
#include "fused.h"
#include "lowering.h"
#include "compute/primitives.h"

namespace sdag
{

namespace graph_detail
{

/// 节点的 sender：载荷是 sender 就原样取出（控制依赖）；是适配器就把输入 sender 喂给它（数据依赖）。
template<class Node>
[[nodiscard]] constexpr auto make_child(Node&& node)
{
	using dependency = node_dependencies_t<Node>;
	using payload_type = decltype(::sdag::payload(::std::forward<Node>(node)));
	static_assert(node_payload<payload_type, input_sender<dependency>>,
		"sdag: 节点载荷既不是 sender，也不是能接受输入 sender 的适配器（例如 ::stdexec::then(f)）");
	if constexpr (sender_payload<payload_type>)
	{
		static_assert(compute::input_count_v<::std::decay_t<payload_type>> == 0, "sdag: 只有控制依赖的节点 sender 里不能出现输入 sender");
		return ::std::decay_t<payload_type>(::sdag::payload(::std::forward<Node>(node)));
	}
	else
	{
		using child = decltype(::sdag::payload(::std::forward<Node>(node))(input_sender<dependency>{}));
		static_assert(compute::input_count_v<child> == 1,
			"sdag: 节点适配器必须把输入 sender 恰好用一次，而且中间只能隔着 stdexec 的算法层："
			"它的值只会被交付一次，repeat / retry 这类会重启子 sender 的适配器不能套在输入上；"
			"不透明的自定义 sender 包着输入时这里会数成 0 次");
		return ::sdag::payload(::std::forward<Node>(node))(input_sender<dependency>{});
	}
}

template<class Node>
using child_of_t = decltype(graph_detail::make_child(::std::declval<Node>()));

}

/// 图算法的标签。dag_t{}(nodes...) 构造图 sender。
struct dag_t
{
	template<graph_node... Nodes>
	[[nodiscard]] constexpr auto operator()(Nodes&&... nodes) const
	{
		using data = graph_detail::dag_data<meta::type_list<node_key_t<Nodes>...>, meta::type_list<node_dependencies_t<Nodes>...>>;
		using children = meta::indexed_tuple<graph_detail::child_of_t<Nodes>...>;
		using sender = graph_sender<data, graph_detail::child_of_t<Nodes>...>;
		// stdexec 靠结构化绑定识别标签；编译器缺少结构化绑定内省时这里会失效，自动降解也就静默关闭，所以显式检查。
		static_assert(::std::same_as<::stdexec::tag_of_t<sender>, dag_t>, "sdag: stdexec 认不出图 sender 的标签（编译器缺少结构化绑定内省）");
		return sender{data{}, children(meta::from_factory, [&] { return graph_detail::make_child(::std::forward<Nodes>(nodes)); }...)};
	}

	/// 所在 domain 会定制 when_all 时，把整图降解成 when_all / split / let_value 组合，让它吃到定制。
	/// 不会时（默认 domain、只定制 bulk 的线程池 domain 等）这个重载不参与，stdexec 用 graph_sender::connect 走融合 op-state。
	template<class OpTag, class Sender, class Env>
		requires (graph_detail::lowering_wanted<OpTag, Sender&&, Env>())
	[[nodiscard]] static constexpr auto transform_sender(OpTag, Sender&& sender, Env const&)
	{
		return ::sdag::lower(::std::forward<Sender>(sender), ::stdexec::when_all, ::exec::split);
	}
};

/// 图 sender 的形状（邻接矩阵、入出度、拓扑序）。
template<class Graph>
constexpr auto shape_v = graph_detail::shape_of_v<Graph&&>;

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
		return ::sdag::lower(dag_t{}(::std::forward<Nodes>(nodes)...), _fanin, _fanout);
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

/// 纯类型层面的写法：static_assert(::stdexec::sender<dag_sender<fused, node<...>, ...>>)。
template<class Policy, class... Nodes>
	requires graph_policy_for<Policy, Nodes...>
using dag_sender = decltype(::std::declval<Policy const&>().compose(::std::declval<Nodes>()...));

}
