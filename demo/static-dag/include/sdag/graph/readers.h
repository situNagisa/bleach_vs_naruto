#pragma once

/// 值的读者：谁读谁的值 → 每个值是共享还是独占。依赖：图的描述、有向无环图、前驱占位符。
///
///   reads_predecessors_v<Self, I>  节点 I 读不读前驱的值：节点 sender 里有占位符就读（数据依赖），没有就只等先后（控制依赖）
///   reader_counts_v<Self>          每个节点"读它的值的后继"有几个。只有控制依赖的后继不算——它们不碰值
///   shared_v<Self, D>              节点 D 的值共不共享。只有一个读者时把值 move 给它（签名写 V），多个读者时都给 V const&（值留在整图里）

#include <array>
#include <cstddef>
#include <utility>

#include "../config.h"
#include "../general/digraph.h"
#include "../protocol/hole.h"
#include "description.h"

namespace sdag::graph
{

template<class Self, size_t I>
inline constexpr bool reads_predecessors_v = protocol::hole_count_v<child_t<Self, I>> != 0;

template<class Self>
inline constexpr auto reader_counts_v = []<size_t... I>(::std::index_sequence<I...>)
{
	constexpr auto shape = shape_of_v<Self>;
	constexpr ::std::array<bool, sizeof...(I)> reads{reads_predecessors_v<Self, I>...};
	::std::array<size_t, sizeof...(I)> counts{};
	for (size_t from = 0; from < shape.count; ++from)
	{
		for (size_t to = 0; to < shape.count; ++to)
		{
			counts[from] += (shape.edge[from][to] && reads[to]) ? 1 : 0;
		}
	}
	return counts;
}(::std::make_index_sequence<count_of_v<Self>>{});

template<class Self, size_t D>
inline constexpr bool shared_v = reader_counts_v<Self>[D] > 1;

}
