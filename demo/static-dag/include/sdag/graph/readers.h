#pragma once

/// 值的读者：谁读谁的值（let）、谁取走谁的值（move）。
///
///   readers_v<Self>.read[J]     节点 J 的值有没有 let / move 读者
///   readers_v<Self>.taker[J]    节点 J 的值被哪个节点 move；没人 move 时等于节点数
///   released_by_v<Self, J>      节点 J 结束时要析构的值（J move 的前驱）
///   access_t<Self, I, K, V>     节点 I 的第 K 个值依赖交出的值类型：move 时 V，let 时 V const&（值留在整图里）
///
/// 规则只有一条：move 就按值交出，并在取走它的节点结束时析构；let 给 const&，值活到整图结束。

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include "../config.h"
#include "../general/sequence.h"
#include "../general/digraph.h"
#include "description.h"

namespace sdag::graph
{

namespace readers_detail
{

/// 每个节点的读者与取走它的节点；顺带记下 move 用法的两种错误（第一个出错的值）。
template<size_t N>
struct readers
{
	::std::array<bool, N> read{};
	::std::array<size_t, N> taker{};
	/// 同一个值被 move 了两次。
	size_t taken_twice = N;
	/// move 它的节点之后还有别的读者（或与它并行的读者）：move 会和那个读者的读冲突。
	size_t reader_after_taker = N;
};

}

template<class Self>
inline constexpr auto readers_v = []
{
	constexpr auto table = table_of_v<Self>;
	constexpr size_t count = table.shape.count;
	constexpr auto reach = general::reach_v<table.shape>;
	readers_detail::readers<count> result{};
	result.taker.fill(count);
	for (size_t node = 0; node < count; ++node)
	{
		for (size_t e = table.first[node]; e < table.first[node + 1]; ++e)
		{
			auto const [from, how] = table.entries[e];
			// 不存在的键（下标 = 节点数）由 graph_data 的形状检查报错，这里跳过。
			if (from >= count || how == mode::wait)
			{
				continue;
			}
			result.read[from] = true;
			if (how == mode::move)
			{
				if (result.taker[from] != count && result.taken_twice == count)
				{
					result.taken_twice = from;
				}
				result.taker[from] = node;
			}
		}
	}
	// move 它的节点必须是它所有其它读者的严格后代：其它读者读完（const&）之后才轮到 move。
	for (size_t node = 0; node < count; ++node)
	{
		for (size_t e = table.first[node]; e < table.first[node + 1]; ++e)
		{
			auto const [from, how] = table.entries[e];
			if (from < count && how == mode::let && result.taker[from] != count
				&& !reach[node][result.taker[from]] && result.reader_after_taker == count)
			{
				result.reader_after_taker = from;
			}
		}
	}
	return result;
}();

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

/// 节点 J 结束时要析构的值：J move 的那些前驱（节点下标，升序）。
template<class Self, size_t J>
inline constexpr auto released_by_v = general::to_array<readers_detail::released_by<Self, J>().size()>(readers_detail::released_by<Self, J>());

template<class Self, size_t I, size_t K, class V>
using access_t = ::std::conditional_t<value_dependencies_v<Self, I>[K].how == mode::move, V, V const&>;

}
