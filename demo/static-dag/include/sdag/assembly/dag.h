#pragma once

/// 公开入口：dag；定义算法标签 dag_t。
///
///   dag<K>(nodes...)                                  建图（K 是键类型，节点由 make<spec>(payload) 给出）；连接时融合成一个 op-state，0 开销
///   dag_t<K>                                          图算法的标签；dag_t<K>{}(nodes...) 构造图 sender
///   make_graph<Tag, K>(nodes...)                      以 Tag 为标签构造图 sender（使用者用自己的标签定制 transform_sender）
///   dag_sender<K, Nodes...>                           纯类型层面的写法
///   shape_v<Graph>                                    图 sender 的形状
///
/// dag_t 没有 transform_sender：图总是走融合 op-state。domain 要接管整张图，可以在自己的 transform_sender 里匹配 dag_t<K>。

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

/// 以 Tag 为算法标签、K 为键类型构造图 sender。标签决定 stdexec 怎么认这张图（tag_of_t / sender_for），以及 transform_sender 的定制。
template<class Tag, key_value K, node_of<K>... Nodes>
[[nodiscard]] constexpr auto make_graph(Nodes&&... nodes)
{
	using data = graph::graph_data<K, ::std::remove_cvref_t<Nodes>::spec...>;
	return [&]<::std::size_t... I>(::std::index_sequence<I...>)
	{
		// 节点 I 的 sender：有 let / move 依赖时把占位符 hole<I> 喂给载荷，否则载荷就是 sender。
		using sender = graph_sender<Tag, data, decltype(protocol::make_node_sender<I, data::table.has_values(I)>(::std::declval<Nodes>()))...>;
		// stdexec 靠结构化绑定识别标签；编译器缺少结构化绑定内省时这里会失效，domain 对标签的匹配也就静默失效，所以显式检查。
		static_assert(::std::same_as<::stdexec::tag_of_t<sender>, Tag>, "sdag: stdexec 认不出图 sender 的标签（编译器缺少结构化绑定内省）");
		static_assert(data::readers.error != graph::move_error::taken_twice, "sdag: 同一个值被 move 了两次；一个值至多只能被一个节点取走");
		static_assert(data::readers.error != graph::move_error::reader_after_taker,
			"sdag: move 某个值的节点必须排在它所有其它读者之后；请把其它读者写进 move 它的节点的依赖（let 或 wait）");
		return sender{{data{}, typename sender::description::children_tuple(general::from_factory,
			[&] { return protocol::make_node_sender<I, data::table.has_values(I)>(::std::forward<Nodes>(nodes)); }...)}};
	}(::std::index_sequence_for<Nodes...>{});
}

/// dag<K>(nodes...)：建图（K 是键类型）；连接时融合成一个 op-state，0 开销。
template<key_value K>
struct dag_t
{
	template<node_of<K>... Nodes>
	[[nodiscard]] constexpr auto operator()(Nodes&&... nodes) const
	{
		return make_graph<dag_t, K>(::std::forward<Nodes>(nodes)...);
	}
};

/// 图 sender 的形状（前驱 / 后继表、入出度、拓扑序、可达关系）。
template<class Graph>
[[nodiscard]] constexpr auto const& shape_of() noexcept
{
	return graph::shape_of<Graph&&>();
}

/// 建图入口：::sdag::dag<K>(nodes...)。是对象而不是函数，所以可以直接当可调用对象传。
template<key_value K>
inline constexpr dag_t<K> dag{};

/// 纯类型层面的写法：static_assert(::stdexec::sender<dag_sender<K, decltype(make<...>(...))...>>)。
template<key_value K, node_of<K>... Nodes>
using dag_sender = decltype(dag_t<K>{}(::std::declval<Nodes>()...));

}
