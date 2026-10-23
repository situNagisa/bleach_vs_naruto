#pragma once

/// 图的类型级查询：给定图 sender 的类型，回答"第 I 个节点是谁、依赖谁、形状如何"。
/// 不看节点 sender 的内容——"谁读谁的值"要看节点里有没有输入叶子，那是 analyze/consumers.h 的事。
///
/// 编译期分析（graph/analyze/）与执行层（execution/）都只通过这里认识图，不直接拆 graph_sender 的模板实参。
///
/// 约定：凡是名为 Self 的模板形参，都是图 sender 的**引用类型**（G&& 或 G const&），值类别决定节点 sender
/// 是被移动还是被拷贝；只关心结构的查询先 remove_cvref。

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../meta_program/type_list.h"
#include "../meta_program/utility.h"
#include "../fwd.h"
#include "node.h"
#include "shape.h"
#include "../meta_program/dependency_indices.h"
#include "../meta_program/sequence.h"

namespace sdag::graph_detail
{

using ::std::size_t;

/// 图 sender 的 data：键表与依赖表，运行期是空对象。形状在这里一次算好并检查（重复键、成环等直接 static_assert）。
template<class Keys, class Dependencies>
struct dag_data
{
	using keys = Keys;
	using dependencies = Dependencies;
	static constexpr auto shape = checked_shape_of_lists<Keys, Dependencies>();
};

template<class Graph>
struct traits;

template<class Data, class... Children>
struct traits<graph_sender<Data, Children...>>
{
	using data = Data;
	using children = meta::type_list<Children...>;
	static constexpr auto shape = Data::shape;
	static constexpr size_t count = sizeof...(Children);
};

template<class Self>
using traits_of = traits<::std::remove_cvref_t<Self>>;

template<class Self>
inline constexpr auto shape_of_v = traits_of<Self>::shape;

template<class Self>
inline constexpr size_t count_of_v = traits_of<Self>::count;

/// 节点 I 的 sender 类型（不带值类别）。
template<class Self, size_t I>
using child_t = meta::at_t<I, typename traits_of<Self>::children>;

/// 节点 I 的 sender 类型，带上 Self 的值类别：右值图交出右值，左值图交出 const&。
template<class Self, size_t I>
using node_sender_t = meta::copy_cvref_t<Self, child_t<Self, I>>;

/// 节点 I 声明的 after<...>
template<class Self, size_t I>
using dependency_t = meta::at_t<I, typename traits_of<Self>::data::dependencies>;

/// 一个 after<...> 里的键换成节点索引（std::array，按声明顺序）。
template<class Self, class Dependency>
inline constexpr auto dependency_indices_v = meta::dependency_indices_v<typename traits_of<Self>::data::keys, Dependency>;

/// 节点 I 的前驱索引（std::index_sequence，按声明顺序）。
template<class Self, size_t I>
using dependencies_t = meta::sequence_t<dependency_indices_v<Self, dependency_t<Self, I>>>;

/// 节点 I 在环境 Env 下的完成签名；算不出来时是 stdexec 给出的错误类型（不是硬错误）。
template<class Self, size_t I, class... Env>
using node_completions_t = decltype(::stdexec::get_completion_signatures<node_sender_t<Self, I>, Env...>());

}
