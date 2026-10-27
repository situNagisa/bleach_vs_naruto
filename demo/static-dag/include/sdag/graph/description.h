#pragma once

/// 图的描述：纯数据——有哪些节点、谁依赖谁；以及 tuple-like 协议。
///
///   graph_data<K, Specs...>            键类型与节点规格，建好的节点表；运行期是空对象。形状在这里一次算好并检查（重复键、成环等直接 static_assert）
///   graph_description<Tag, Data, Children...>
///                                      图的数据：data + 节点 sender。按 [Tag, data, 节点 sender...] 实现标准的 tuple-like 协议
///                                      （std::tuple_size / std::tuple_element / 成员 get<I>），和 stdexec 自己的算法 sender 是同一个形状，
///                                      所以 stdexec 靠结构化绑定就能认出它的标签。标签由使用者给出（对算法标签泛型），描述不认识 dag_t
///
/// 类型级查询（凡是名为 Self 的模板形参，都是图的**引用类型** G&& 或 G const&，值类别决定节点 sender 被移动还是被拷贝）：
///   description_of<Self>               Self 去引用后的 graph_description（图 sender 的基类）
///   shape_of_v<Self>, count_of_v<Self>
///   child_t<Self, I>                   节点 I 的 sender 类型（不带值类别）
///   node_sender_t<Self, I>             节点 I 的 sender 类型，带上 Self 的值类别
///   node_completions_t<Self, I, Env...>  节点 I 的完成签名
///   table_of_v<Self>                   节点表（node/node_graph.h 的 graph_table）
///   value_dependencies_v<Self, I>      节点 I 的 let / move 依赖（前驱下标 + mode，按声明顺序），它的占位符就是 hole<I>
///   data_dependencies_t<Self, I>       节点 I 的 let / move 前驱下标（index_sequence）
///   control_dependencies_t<Self, I>    节点 I 的 wait 前驱下标（index_sequence）

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/sequence.h"
#include "../general/tuple.h"
#include "../general/cvref.h"
#include "../general/digraph.h"
#include "../node/node_graph.h"

namespace sdag::graph
{

using ::std::size_t;

/// 图的数据：键类型 K 下的一组节点规格，建好的表（graph_table）在这里一次算好并检查。运行期是空对象。
template<class K, auto... Specs>
struct graph_data
{
	using key_type = K;
	static constexpr auto table = checked_table<K, Specs...>();
	static constexpr auto shape = table.shape;
};

template<class Tag, class Data, class... Children>
struct graph_description
{
	/// 派生类型（图 sender）也经由它找回描述：description_of<Self> = remove_cvref_t<Self>::description。
	using description = graph_description;
	using data_type = Data;
	using children = general::type_list<Children...>;
	using children_tuple = general::indexed_tuple<Children...>;

	SDAG_NO_UNIQUE_ADDRESS Data _data;
	/// 节点 sender。构造者用工厂把它们直接构造在这里，不经过临时对象再移动。
	children_tuple _children;

	/// tuple-like 协议：[0] 是标签，[1] 是 data，[2...] 是节点 sender。返回的引用跟随 self 的值类别。
	template<size_t I, class Self>
	[[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept
	{
		if constexpr (I == 0)
		{
			return Tag{};
		}
		else if constexpr (I == 1)
		{
			return (::std::forward_like<Self>(self._data));
		}
		else
		{
			return general::get<I - 2>(::std::forward_like<Self>(self._children));
		}
	}
};

template<class Self>
using description_of = typename ::std::remove_cvref_t<Self>::description;

template<class Self>
inline constexpr auto shape_of_v = description_of<Self>::data_type::shape;

template<class Self>
inline constexpr auto table_of_v = description_of<Self>::data_type::table;

template<class Self>
inline constexpr size_t count_of_v = description_of<Self>::children::size;

template<class Self, size_t I>
using child_t = general::at_t<I, typename description_of<Self>::children>;

template<class Self, size_t I>
using node_sender_t = general::copy_cvref_t<Self, child_t<Self, I>>;

/// 节点 I 在环境 Env... 下的完成签名；算不出来时是 stdexec 给出的错误类型（不是硬错误）。
template<class Self, size_t I, class... Env>
using node_completions_t = decltype(::stdexec::get_completion_signatures<node_sender_t<Self, I>, Env...>());

namespace description_detail
{

/// 节点 I 中用法满足 wanted 的依赖（按声明顺序）；values 为真时只要 let / move，为假时只要 wait。
template<class Self, size_t I, bool Values>
[[nodiscard]] consteval auto select()
{
	constexpr auto table = table_of_v<Self>;
	constexpr size_t begin = table.first[I];
	constexpr size_t end = table.first[I + 1];
	constexpr size_t size = [&]
	{
		size_t n = 0;
		for (size_t e = begin; e < end; ++e)
		{
			n += ((table.entries[e].how != mode::wait) == Values) ? 1 : 0;
		}
		return n;
	}();
	::std::array<dependency_entry, size> result{};
	size_t cursor = 0;
	for (size_t e = begin; e < end; ++e)
	{
		if ((table.entries[e].how != mode::wait) == Values)
		{
			result[cursor++] = table.entries[e];
		}
	}
	return result;
}

template<auto Entries>
inline constexpr auto froms_v = []
{
	::std::array<size_t, Entries.size()> result{};
	for (size_t index = 0; index < Entries.size(); ++index)
	{
		result[index] = Entries[index].from;
	}
	return result;
}();

}

/// 节点 I 的值依赖（let / move，按声明顺序，交给载荷的就是这些值）：dependency_entry 的 std::array。
template<class Self, size_t I>
inline constexpr auto value_dependencies_v = description_detail::select<Self, I, true>();

/// 节点 I 的值依赖的前驱下标、wait 依赖的前驱下标（std::index_sequence）。
template<class Self, size_t I>
using data_dependencies_t = general::sequence_t<description_detail::froms_v<value_dependencies_v<Self, I>>>;

template<class Self, size_t I>
using control_dependencies_t = general::sequence_t<description_detail::froms_v<description_detail::select<Self, I, false>()>>;

}

/// tuple-like 协议的标准库一侧。派生类型（图 sender）各自再特化一份，转到这里。
template<class Tag, class Data, class... Children>
struct std::tuple_size<::sdag::graph::graph_description<Tag, Data, Children...>>
	: ::std::integral_constant<::std::size_t, 2 + sizeof...(Children)>
{
};

template<::std::size_t I, class Tag, class Data, class... Children>
struct std::tuple_element<I, ::sdag::graph::graph_description<Tag, Data, Children...>>
{
	using type = ::sdag::general::at_t<I, ::sdag::general::type_list<Tag, Data, Children...>>;
};
