#pragma once

/// 存储计划：哪些值要存进整图、存的时候会不会抛、唯一汇点是否直接转发。
///
/// storage_plan<Self, Env>
///   forward[J]                  节点 J 的值不存槽位、完成时直接转发给整图的接收者：J 是唯一的汇点
///   slot_needed[J]              节点 J 的值要不要存：有值、不直接转发，并且（是汇点，或者有后继读它的值）
///   layout                      值槽的复用：存值的节点划分成链（general/chains.h），同一条链的值依次共用一块存储
///                               （u 先于 v：u 被 move，且取走它的节点是 v 的严格祖先——u 析构之后 v 才产生值）
///   nothrow_store               所有要存的值、所有错误存进 op-state 时都不抛
///   can_fail                    有节点可能出错 / 停止 / 存值时抛异常——决定整图要不要自己的停止源
///
/// 这里只依赖节点完成分析的结果，不碰环境；所以"在哪个环境下算"由调用者（graph/visibility.h）决定。

#include <array>
#include <cstddef>

#include "../config.h"
#include "../general/digraph.h"
#include "../general/chains.h"
#include "description.h"
#include "readers.h"
#include "analysis.h"

namespace sdag::graph
{

template<class Self, class Env>
struct storage_plan
{
	using analysis = node_analysis<Self, Env>;
	static constexpr auto shape = shape_of_v<Self>;

	static constexpr auto forward = []<size_t... I>(::std::index_sequence<I...>)
	{
		return ::std::array<bool, sizeof...(I)>{(shape.is_sink(I) && shape.sink_count() == 1)...};
	}(::std::make_index_sequence<count_of_v<Self>>{});

	static constexpr auto slot_needed = []<size_t... I>(::std::index_sequence<I...>)
	{
		constexpr auto read = readers_v<Self>.read;
		return ::std::array<bool, sizeof...(I)>{
			(analysis::template values_t<I>::size != 0 && !forward[I] && (shape.is_sink(I) || read[I]))...};
	}(::std::make_index_sequence<count_of_v<Self>>{});

	static constexpr auto layout = []
	{
		constexpr size_t count = count_of_v<Self>;
		constexpr auto taker = readers_v<Self>.taker;
		constexpr auto reach = general::reach_v<shape>;
		::std::array<::std::array<bool, count>, count> before{};
		for (size_t u = 0; u < count; ++u)
		{
			for (size_t v = 0; v < count; ++v)
			{
				before[u][v] = taker[u] != count && reach[taker[u]][v];
			}
		}
		return general::min_chain_cover(before, slot_needed);
	}();

	static constexpr bool nothrow_store = []
	{
		for (size_t index = 0; index < count_of_v<Self>; ++index)
		{
			if (!analysis::nothrow_errors[index] || (slot_needed[index] && !analysis::nothrow_values[index]))
			{
				return false;
			}
		}
		return true;
	}();

	static constexpr bool can_fail = analysis::any_errors || analysis::any_stopped || !nothrow_store;
};

}
