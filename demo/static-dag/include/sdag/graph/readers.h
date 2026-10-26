#pragma once

/// 值的读者：谁读谁的值 → 每个值是共享还是独占。依赖：图的描述、有向无环图。
///
///   reader_counts_v<Self>          每个节点"读它的值的后继"有几个：只数把它写在 data<...> 里的后继，写在 control<...> 里的不碰值
///   shared_v<Self, D>              节点 D 的值共不共享。只有一个读者时把值 move 给它（签名写 V），多个读者时都给 V const&（值留在整图里）

#include <array>
#include <cstddef>
#include <utility>

#include "../config.h"
#include "../general/digraph.h"
#include "description.h"

namespace sdag::graph
{

template<class Self>
inline constexpr auto reader_counts_v = []<size_t... I>(::std::index_sequence<I...>)
{
	::std::array<size_t, sizeof...(I)> counts{};
	(
		[&]
		{
			for (size_t from : dependency_indices_v<Self, data_dependency_t<Self, I>>)
			{
				// 不存在的键（下标 = 节点数）由 graph_data 的形状检查报错，这里跳过，免得越界盖掉那条诊断。
				if (from < sizeof...(I))
				{
					++counts[from];
				}
			}
		}(),
		...);
	return counts;
}(::std::make_index_sequence<count_of_v<Self>>{});

template<class Self, size_t D>
inline constexpr bool shared_v = reader_counts_v<Self>[D] > 1;

}
