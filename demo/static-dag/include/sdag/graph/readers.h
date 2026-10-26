#pragma once

/// 值的读者：谁读谁的值、谁取走谁的值。
///
///   readers_v<Self>.read[J]     节点 J 的值有没有数据读者（写在某个 data<...> 里）
///   readers_v<Self>.taker[J]    节点 J 的值被哪个节点 take；没人 take 时等于节点数
///   released_by_v<Self, J>      节点 J 结束时要析构的值（J take 的前驱）
///   access_t<Dependency, K, V>  data<...> 的第 K 个前驱交出的值类型：take 时 V（move），否则 V const&（值留在整图里）
///
/// 规则只有一条：写了 take 就 move，并在取走它的节点结束时析构；没写就给 const&，值活到整图结束。

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include "../config.h"
#include "../general/sequence.h"
#include "description.h"

namespace sdag::graph
{

namespace readers_detail
{

/// 每个节点的数据读者，以及取走它的节点；顺带记下 take 用法的两种错误（第一个出错的值）。
template<size_t N>
struct readers
{
	::std::array<bool, N> read{};
	::std::array<size_t, N> taker{};
	/// 同一个值被 take 了两次。
	size_t taken_twice = N;
	/// take 它的节点之后还有别的读者（或与它并行的读者）：take 会和那个读者的读冲突。
	size_t reader_after_taker = N;
};

}

template<class Self>
inline constexpr auto readers_v = []<size_t... I>(::std::index_sequence<I...>)
{
	constexpr size_t count = sizeof...(I);
	readers_detail::readers<count> result{};
	result.taker.fill(count);
	(
		[&]
		{
			constexpr auto from = dependency_indices_v<Self, data_dependency_t<Self, I>>;
			for (size_t k = 0; k < from.size(); ++k)
			{
				// 不存在的键（下标 = 节点数）由 graph_data 的形状检查报错，这里跳过。
				if (from[k] < count)
				{
					result.read[from[k]] = true;
					if (data_dependency_t<Self, I>::taken[k])
					{
						if (result.taker[from[k]] != count && result.taken_twice == count)
						{
							result.taken_twice = from[k];
						}
						result.taker[from[k]] = I;
					}
				}
			}
		}(),
		...);
	// take 它的节点必须是它所有其它读者的严格后代：其它读者读完（const&）之后才轮到 take。
	constexpr auto shape = shape_of_v<Self>;
	constexpr auto reach = general::reach_v<shape>;
	(
		[&]
		{
			for (size_t from : dependency_indices_v<Self, data_dependency_t<Self, I>>)
			{
				if (from < count && result.taker[from] != count && result.taker[from] != I
					&& !reach[I][result.taker[from]] && result.reader_after_taker == count)
				{
					result.reader_after_taker = from;
				}
			}
		}(),
		...);
	return result;
}(::std::make_index_sequence<count_of_v<Self>>{});

namespace readers_detail
{

template<class Self, size_t J>
[[nodiscard]] consteval ::std::vector<size_t> released_by()
{
	::std::vector<size_t> result;
	for (size_t node = 0; node < count_of_v<Self>; ++node)
	{
		if (readers_v<Self>.taker[node] == J)
		{
			result.push_back(node);
		}
	}
	return result;
}

}

/// 节点 J 结束时要析构的值：J take 的那些前驱（节点下标，升序）。
template<class Self, size_t J>
inline constexpr auto released_by_v = general::to_array<readers_detail::released_by<Self, J>().size()>(readers_detail::released_by<Self, J>());

template<class Dependency, size_t K, class V>
using access_t = ::std::conditional_t<Dependency::taken[K], V, V const&>;

}
