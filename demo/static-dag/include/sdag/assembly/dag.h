#pragma once

/// 公开入口：dag；定义算法标签 dag_t。
///
///   dag(nodes...)                                     建图；连接时融合成一个 op-state，0 开销
///   dag_t                                             图算法的标签；dag_t{}(nodes...) 构造图 sender
///   make_graph<Tag>(nodes...)                         以 Tag 为标签构造图 sender（使用者用自己的标签定制 transform_sender）
///   dag_sender<Nodes...>                              纯类型层面的写法
///   shape_v<Graph>                                    图 sender 的形状
///
/// dag_t 没有 transform_sender：图总是走融合 op-state。domain 要接管整张图，可以在自己的 transform_sender 里匹配 dag_t。

#include <concepts>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/tuple.h"
#include "../node/node.h"
#include "../node/node_graph.h"
#include "../protocol/node_sender.h"
#include "../graph/description.h"
#include "../graph/readers.h"
#include "../graph/slots.h"
#include "graph_sender.h"

namespace sdag
{

/// 以 Tag 为算法标签构造图 sender。标签决定 stdexec 怎么认这张图（tag_of_t / sender_for），以及 transform_sender 的定制。
template<class Tag, graph_node... Nodes>
[[nodiscard]] constexpr auto make_graph(Nodes&&... nodes)
{
	using data = graph::graph_data<keys_of_v<Nodes...>, general::type_list<node_dependencies_t<Nodes>...>>;
	using children = general::indexed_tuple<protocol::node_sender_of_t<Nodes>...>;
	using sender = graph_sender<Tag, data, protocol::node_sender_of_t<Nodes>...>;
	// stdexec 靠结构化绑定识别标签；编译器缺少结构化绑定内省时这里会失效，domain 对标签的匹配也就静默失效，所以显式检查。
	static_assert(::std::same_as<::stdexec::tag_of_t<sender>, Tag>, "sdag: stdexec 认不出图 sender 的标签（编译器缺少结构化绑定内省）");
	static_assert(graph::readers_v<sender&&>.taken_twice == sizeof...(Nodes), "sdag: 同一个值被 take 了两次；一个值至多只能被一个节点取走");
	static_assert(graph::readers_v<sender&&>.reader_after_taker == sizeof...(Nodes),
		"sdag: take 某个值的节点必须排在它所有其它读者之后；请把其它读者写进 take 它的节点的 data<...> 或 control<...>");
	return sender{{data{}, children(general::from_factory, [&] { return protocol::make_node_sender(::std::forward<Nodes>(nodes)); }...)}};
}

struct dag_t
{
	template<graph_node... Nodes>
	[[nodiscard]] constexpr auto operator()(Nodes&&... nodes) const
	{
		return make_graph<dag_t>(::std::forward<Nodes>(nodes)...);
	}
};

/// 图 sender 的形状（邻接矩阵、入出度、拓扑序）。
template<class Graph>
constexpr auto shape_v = graph::shape_of_v<Graph&&>;

/// 建图入口：::sdag::dag(nodes...)。是对象而不是函数，所以可以直接当可调用对象传。
inline constexpr dag_t dag{};

/// 纯类型层面的写法：static_assert(::stdexec::sender<dag_sender<node<...>, ...>>)。
template<graph_node... Nodes>
using dag_sender = decltype(dag_t{}(::std::declval<Nodes>()...));

}
