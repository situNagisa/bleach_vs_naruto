#pragma once

/// 图的描述：纯数据——有哪些节点、谁依赖谁；以及 tuple-like 协议。
/// 依赖：cv/引用转抄、有向无环图、节点集合成图、编译期序列、不可移动元组、类型列表。
///
///   graph_data<Keys, Dependencies>     键表与依赖表，运行期是空对象。形状在这里一次算好并检查（重复键、成环等直接 static_assert）
///   graph_description<Tag, Data, Children...>
///                                      图的数据：data + 节点 sender。按 [Tag, data, 节点 sender...] 实现标准的 tuple-like 协议
///                                      （std::tuple_size / std::tuple_element / 成员 get<I>），和 stdexec 自己的算法 sender 是同一个形状，
///                                      所以 stdexec 靠结构化绑定就能认出它的标签。标签由使用者给出（对算法标签泛型），描述不认识 dag_t
///
/// 类型级查询（凡是名为 Self 的模板形参，都是图的**引用类型** G&& 或 G const&，值类别决定节点 sender 被移动还是被拷贝）：
///   description_of<Self>               Self 去引用后的 graph_description 基类
///   shape_of_v<Self>, count_of_v<Self>
///   child_t<Self, I>                   节点 I 的 sender 类型（不带值类别）
///   node_sender_t<Self, I>             节点 I 的 sender 类型，带上 Self 的值类别
///   dependency_t<Self, I>              节点 I 声明的 after<...>
///   dependency_indices_v<Self, after<...>>   after<...> 里的键换成节点索引（std::array）
///   dependencies_t<Self, I>            节点 I 的前驱索引（std::index_sequence，按声明顺序）

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

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

template<class Keys, class Dependencies>
struct graph_data
{
	using keys = Keys;
	using dependencies = Dependencies;
	static constexpr auto shape = checked_shape_of_lists<Keys, Dependencies>();
};

template<class Tag, class Data, class... Children>
struct graph_description
{
	using tag_type = Tag;
	using data_type = Data;
	using children = general::type_list<Children...>;
	static constexpr auto shape = Data::shape;
	static constexpr size_t count = sizeof...(Children);

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

namespace description_detail
{

/// 派生类型（图 sender）也要能用 description_of 找回描述：按基类匹配。
template<class Tag, class Data, class... Children>
graph_description<Tag, Data, Children...> base_description(graph_description<Tag, Data, Children...> const&);

}

template<class Self>
using description_of = decltype(description_detail::base_description(::std::declval<::std::remove_cvref_t<Self> const&>()));

template<class Self>
inline constexpr auto shape_of_v = description_of<Self>::shape;

template<class Self>
inline constexpr size_t count_of_v = description_of<Self>::count;

template<class Self, size_t I>
using child_t = general::at_t<I, typename description_of<Self>::children>;

template<class Self, size_t I>
using node_sender_t = general::copy_cvref_t<Self, child_t<Self, I>>;

template<class Self, size_t I>
using dependency_t = general::at_t<I, typename description_of<Self>::data_type::dependencies>;

template<class Self, class Dependency>
inline constexpr auto dependency_indices_v = key_indices_v<typename description_of<Self>::data_type::keys, Dependency>;

template<class Self, size_t I>
using dependencies_t = general::sequence_t<dependency_indices_v<Self, dependency_t<Self, I>>>;

}

/// tuple-like 协议的标准库一侧。派生类型（图 sender）各自再特化一份，转到这里。
template<class Tag, class Data, class... Children>
struct std::tuple_size<::sdag::graph::graph_description<Tag, Data, Children...>>
	: ::std::integral_constant<::std::size_t, 2 + sizeof...(Children)>
{
};

template<class Tag, class Data, class... Children>
struct std::tuple_element<0, ::sdag::graph::graph_description<Tag, Data, Children...>>
{
	using type = Tag;
};

template<class Tag, class Data, class... Children>
struct std::tuple_element<1, ::sdag::graph::graph_description<Tag, Data, Children...>>
{
	using type = Data;
};

template<::std::size_t I, class Tag, class Data, class... Children>
	requires (I >= 2)
struct std::tuple_element<I, ::sdag::graph::graph_description<Tag, Data, Children...>>
{
	using type = Children...[I - 2];
};
