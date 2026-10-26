#pragma once

/// 图的描述：纯数据——有哪些节点、谁依赖谁；以及 tuple-like 协议。
///
///   graph_data<Keys, Dependencies>     键表（std::array）与依赖表，运行期是空对象。形状在这里一次算好并检查（重复键、成环等直接 static_assert）
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
///   dependency_t<Self, I>              节点 I 的 dependencies<data<...>, control<...>>
///   data_dependency_t<Self, I>         节点 I 的 data<...>（它的前驱占位符就是 hole<data<...>>）
///   dependency_indices_v<Self, L>      data<...> / control<...> 的前驱键（L::keys，take{k} 已换成 k）换成节点索引（std::array）
///   data_dependencies_t<Self, I>       只含数据依赖
///   control_dependencies_t<Self, I>    只含控制依赖

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

template<auto Keys, class Dependencies>
struct graph_data
{
	static constexpr auto keys = Keys;
	using dependencies = Dependencies;
	static constexpr auto shape = checked_shape_of_lists<Keys, Dependencies>();
};

template<class Tag, class Data, class... Children>
struct graph_description
{
	/// 派生类型（图 sender）也经由它找回描述：description_of<Self> = remove_cvref_t<Self>::description。
	using description = graph_description;
	using data_type = Data;
	using children = general::type_list<Children...>;

	SDAG_NO_UNIQUE_ADDRESS Data _data;
	/// 节点 sender。构造者用工厂把它们直接构造在这里，不经过临时对象再移动。
	general::indexed_tuple<Children...> _children;

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
inline constexpr size_t count_of_v = description_of<Self>::children::size;

template<class Self, size_t I>
using child_t = general::at_t<I, typename description_of<Self>::children>;

template<class Self, size_t I>
using node_sender_t = general::copy_cvref_t<Self, child_t<Self, I>>;

/// 节点 I 在环境 Env... 下的完成签名；算不出来时是 stdexec 给出的错误类型（不是硬错误）。
template<class Self, size_t I, class... Env>
using node_completions_t = decltype(::stdexec::get_completion_signatures<node_sender_t<Self, I>, Env...>());

template<class Self, size_t I>
using dependency_t = general::at_t<I, typename description_of<Self>::data_type::dependencies>;

template<class Self, class Dependency>
inline constexpr auto dependency_indices_v = key_indices_v<description_of<Self>::data_type::keys, Dependency::keys>;

template<class Self, size_t I>
using data_dependency_t = typename dependency_t<Self, I>::data_type;

template<class Self, size_t I>
using data_dependencies_t = general::sequence_t<dependency_indices_v<Self, data_dependency_t<Self, I>>>;

template<class Self, size_t I>
using control_dependencies_t = general::sequence_t<dependency_indices_v<Self, typename dependency_t<Self, I>::control_type>>;

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
