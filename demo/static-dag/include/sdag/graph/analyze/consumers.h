#pragma once

/// 值的消费者：每个节点的值有几个后继要读，以及因此要共享还是独占。
///
///   consumes_input_v<Self, I>   节点 I 读不读前驱的值：节点 sender 里有输入叶子就读（数据依赖），没有就只等先后（控制依赖）
///   consumer_counts_v<Self>     每个节点"读它的值的后继"有几个。只有控制依赖的后继不算——它们不碰值
///   shared_v<Self, D>           节点 D 的值共不共享。只有一个读者时把值 move 给它（签名写 V），多个读者时都给 V const&（值留在整图里）

#include <array>
#include <cstddef>
#include <utility>

#include "../../config.h"
#include "../traits.h"
#include "sender_expression.h"

namespace sdag::graph_detail
{

/// 节点 I 读不读前驱的值：节点 sender 里有输入叶子就读（数据依赖），没有就只等先后（控制依赖）。
template<class Self, size_t I>
inline constexpr bool consumes_input_v = analyze::input_count_v<child_t<Self, I>> != 0;

/// 每个节点"读它的值的后继"有几个。只有控制依赖的后继不算——它们不碰值。
template<class Self>
inline constexpr auto consumer_counts_v = []<size_t... I>(::std::index_sequence<I...>)
{
	constexpr auto shape = shape_of_v<Self>;
	constexpr ::std::array<bool, sizeof...(I)> consumes{consumes_input_v<Self, I>...};
	::std::array<size_t, sizeof...(I)> counts{};
	for (size_t from = 0; from < shape.count; ++from)
	{
		for (size_t to = 0; to < shape.count; ++to)
		{
			counts[from] += (shape.edge[from][to] && consumes[to]) ? 1 : 0;
		}
	}
	return counts;
}(::std::make_index_sequence<count_of_v<Self>>{});

/// 节点 D 的值共不共享。只有一个读者时把值 move 给它（签名写 V），多个读者时都给 V const&（值留在整图里）。
template<class Self, size_t D>
inline constexpr bool shared_v = consumer_counts_v<Self>[D] > 1;

}
