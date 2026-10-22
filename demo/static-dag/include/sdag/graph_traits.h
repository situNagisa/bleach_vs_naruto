#pragma once

/// 图的类型级查询：给定图 sender 的类型，回答"第 I 个节点是谁、依赖谁、谁读它的值"。
///
/// 编译期计算层（compute/）、整图状态（node_input.h）、属性（attributes.h）、融合 op-state（fused.h）、
/// 降解（lowering.h）都只通过这里认识图，不直接拆 graph_sender 的模板实参。
///
/// 约定：凡是名为 Self 的模板形参，都是图 sender 的**引用类型**（G&& 或 G const&），值类别决定节点 sender
/// 是被移动还是被拷贝；只关心结构的查询先 remove_cvref。

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "meta.h"
#include "fwd.h"
#include "node.h"
#include "shape.h"
#include "compute/primitives.h"

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
inline constexpr auto dependency_indices_v = compute::dependency_indices_v<typename traits_of<Self>::data::keys, Dependency>;

/// 节点 I 的前驱索引（std::index_sequence，按声明顺序）。
template<class Self, size_t I>
using dependencies_t = compute::sequence_t<dependency_indices_v<Self, dependency_t<Self, I>>>;

/// 节点 I 在环境 Env 下的完成签名；算不出来时是 stdexec 给出的错误类型（不是硬错误）。
template<class Self, size_t I, class... Env>
using node_completions_t = decltype(::stdexec::get_completion_signatures<node_sender_t<Self, I>, Env...>());

// ---------------------------------------------------------------- 值的消费者

/// 节点 I 读不读前驱的值：节点 sender 里有输入叶子就读（数据依赖），没有就只等先后（控制依赖）。
template<class Self, size_t I>
inline constexpr bool consumes_input_v = compute::input_count_v<child_t<Self, I>> != 0;

/// 每个节点"读它的值的后继"有几个。只有控制依赖的后继不算——它们不碰值。
template<class Self>
inline constexpr auto consumer_counts_v = []<size_t... I>(::std::index_sequence<I...>)
{
	constexpr auto shape = shape_of_v<Self>;
	constexpr ::std::array<bool, sizeof...(I)> consumes{consumes_input_v<Self, I>...};
	::std::array<size_t, sizeof...(I)> counts{};
	for (size_t from = 0; from < shape.count; ++from)
	{
		for (size_t to = 0; to < shape.count; ++to)
		{
			counts[from] += (shape.edge[from][to] && consumes[to]) ? 1 : 0;
		}
	}
	return counts;
}(::std::make_index_sequence<count_of_v<Self>>{});

/// 节点 D 的值共不共享。只有一个读者时把值 move 给它（签名写 V），多个读者时都给 V const&（值留在整图里）。
template<class Self, size_t D>
inline constexpr bool shared_v = consumer_counts_v<Self>[D] > 1;

// ---------------------------------------------------------------- 降解与属性共用的小部件

/// 只有控制依赖的节点降解后是 let_value(fanin(前驱...), constant_sender{载荷})：忽略前驱的值，直接给出载荷 sender。
template<class Sender>
struct constant_sender
{
	Sender _sender;

	template<class... Args>
	[[nodiscard]] constexpr Sender operator()(Args&&...) const& noexcept(::std::is_nothrow_copy_constructible_v<Sender>)
	{
		return _sender;
	}

	template<class... Args>
	[[nodiscard]] constexpr Sender operator()(Args&&...) && noexcept(::std::is_nothrow_move_constructible_v<Sender>)
	{
		return ::std::move(_sender);
	}
};

/// 节点在 Env 下的值完成域（公开接口 get_completion_domain<set_value_t>）。给不出时替换失败，供 SFINAE 判断。
template<class Node, class Env>
using node_domain_t = decltype(::stdexec::get_completion_domain<::stdexec::set_value_t>(
	::stdexec::get_env(::std::declval<Node const&>()), ::std::declval<Env const&>()));

}
