#pragma once

/// 存储计划：哪些值要存进整图、存的时候会不会抛、唯一汇点是否直接转发。
/// 依赖：节点完成分析、图的描述、有向无环图、值的读者、类型列表。
///
/// storage_plan<Self, Env>
///   direct_sink / sink_index    只有一个汇点时它的值不存槽位，完成时直接转发给整图的接收者
///   slot_needed[J]              节点 J 的值要不要存：有值，并且（是要存值的汇点，或者有后继读它的值）
///   nothrow_store               所有要存的值、所有错误存进 op-state 时都不抛
///   can_fail                    有节点可能出错 / 停止 / 存值时抛异常——决定整图要不要自己的停止源
///
/// 这里只依赖节点完成分析的结果，不碰环境；所以"在哪个环境下算"由调用者（graph/visibility.h）决定。

#include <array>
#include <cstddef>

#include "../config.h"
#include "../general/digraph.h"
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

	static constexpr bool direct_sink = shape.sink_count() == 1;
	static constexpr size_t sink_index = direct_sink ? general::sinks_v<shape>[0] : size_t{0};

	static constexpr auto slot_needed = []<size_t... I>(::std::index_sequence<I...>)
	{
		constexpr auto readers = reader_counts_v<Self>;
		return ::std::array<bool, sizeof...(I)>{
			(analysis::template values_t<I>::size != 0 && ((shape.is_sink(I) && !direct_sink) || readers[I] != 0))...};
	}(::std::make_index_sequence<count_of_v<Self>>{});

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
