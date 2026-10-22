#pragma once

/// 图 sender 的类型：[dag_t, dag_data<键表, 依赖表>, 节点 sender...]。
///
/// 它实现标准的 tuple-like 协议（std::tuple_size / std::tuple_element / 成员 get<I>），所以 stdexec 认得它的标签
/// （tag_of_t<G> == dag_t，exec::sender_for<G, dag_t> 成立），domain 也能按 [tag, data, child...] 结构化绑定它——
/// 与 stdexec 自己的算法 sender 是同一个形状，但不依赖 stdexec 的内部类型。
///
/// 这里只放类型与特征；完成签名、属性、连接的实现分别在 graph.h 与 fused.h。

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "meta.h"
#include "fwd.h"
#include "node.h"
#include "shape.h"
#include "compute/primitives.h"

namespace sdag
{

namespace fused_detail
{

template<class Self, class Receiver>
struct fused_operation;

}

namespace graph_detail
{

template<class Data, class... Children>
struct attrs;

template<class Self, class... Env>
[[nodiscard]] consteval auto completions_of();

/// 图 sender 的 data：只有类型，运行期是空对象。
template<class Keys, class Dependencies>
struct dag_data
{
	using keys = Keys;
	using dependencies = Dependencies;
	static constexpr auto shape = checked_shape_of_lists<Keys, Dependencies>();
	static constexpr ::std::size_t count = shape.count;
};

}

inline namespace SDAG_ABI_NAMESPACE
{

template<class Data, class... Children>
struct graph_sender
{
	using sender_concept = ::stdexec::sender_t;

	SDAG_NO_UNIQUE_ADDRESS Data _data;
	/// 节点 sender 由工厂直接构造在这里（不经过临时对象再移动）。
	meta::indexed_tuple<Children...> _children;

	/// tuple-like 协议：[0] 是标签，[1] 是 data，[2...] 是节点 sender。值类别跟随 self。
	template<::std::size_t I, class Self>
	[[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept
	{
		if constexpr (I == 0)
		{
			// 写成依赖名：这里 dag_t 还没定义完。
			return typename ::std::tuple_element<0, ::std::remove_cvref_t<Self>>::type{};
		}
		else if constexpr (I == 1)
		{
			return (::std::forward<Self>(self)._data);
		}
		else
		{
			return meta::get<I - 2>(::std::forward<Self>(self)._children);
		}
	}

	[[nodiscard]] constexpr graph_detail::attrs<Data, Children...> get_env() const noexcept
	{
		return {};
	}

	/// 非 const 左值与 const 左值一样按拷贝连接，所以签名也按 const& 算（与 connect 的两个重载对应）。
	template<class Self, class... Env>
	[[nodiscard]] static consteval auto get_completion_signatures()
	{
		if constexpr (::std::is_lvalue_reference_v<Self> || ::std::is_const_v<::std::remove_reference_t<Self>>)
		{
			return graph_detail::completions_of<graph_sender const&, Env...>();
		}
		else
		{
			return graph_detail::completions_of<graph_sender&&, Env...>();
		}
	}

	template<::stdexec::receiver Receiver>
	[[nodiscard]] fused_detail::fused_operation<graph_sender&&, Receiver> connect(Receiver receiver) &&
		noexcept(::std::is_nothrow_constructible_v<fused_detail::fused_operation<graph_sender&&, Receiver>, graph_sender&&, Receiver>)
	{
		return fused_detail::fused_operation<graph_sender&&, Receiver>(::std::move(*this), ::std::move(receiver));
	}

	template<::stdexec::receiver Receiver>
		requires ::std::copy_constructible<graph_sender>
	[[nodiscard]] fused_detail::fused_operation<graph_sender const&, Receiver> connect(Receiver receiver) const&
		noexcept(::std::is_nothrow_constructible_v<fused_detail::fused_operation<graph_sender const&, Receiver>, graph_sender const&, Receiver>)
	{
		return fused_detail::fused_operation<graph_sender const&, Receiver>(*this, ::std::move(receiver));
	}
};

}

}

template<class Data, class... Children>
struct std::tuple_size<::sdag::graph_sender<Data, Children...>> : ::std::integral_constant<::std::size_t, 2 + sizeof...(Children)>
{
};

template<class Data, class... Children>
struct std::tuple_element<0, ::sdag::graph_sender<Data, Children...>>
{
	using type = ::sdag::dag_t;
};

template<class Data, class... Children>
struct std::tuple_element<1, ::sdag::graph_sender<Data, Children...>>
{
	using type = Data;
};

template<::std::size_t I, class Data, class... Children>
	requires (I >= 2)
struct std::tuple_element<I, ::sdag::graph_sender<Data, Children...>>
{
	using type = Children...[I - 2];
};

namespace sdag::graph_detail
{

using ::std::size_t;

// ---------------------------------------------------------------- 图的特征

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

/// Self 永远是图 sender 的引用类型（X&& 或 X const&）；特征按去掉引用后的类型取。
template<class Self>
using traits_of = traits<::std::remove_cvref_t<Self>>;

template<class Self>
inline constexpr auto shape_of_v = traits_of<Self>::shape;

template<class Self>
inline constexpr size_t count_of_v = traits_of<Self>::count;

template<class Self, size_t I>
using child_t = meta::at_t<I, typename traits_of<Self>::children>;

/// 节点 I 的 sender，带上 Self 的值类别：右值图 move 子 sender，const& 图拷贝它们。
template<class Self, size_t I>
using node_sender_t = meta::copy_cvref_t<Self, child_t<Self, I>>;

/// 节点 I 的 after<...>
template<class Self, size_t I>
using dependency_t = meta::at_t<I, typename traits_of<Self>::data::dependencies>;

/// 节点 I 的前驱索引（after<...> 声明顺序），以及对应的 index_sequence。
template<class Self, class Dependency>
inline constexpr auto dependency_indices_v = compute::dependency_indices_v<typename traits_of<Self>::data::keys, Dependency>;

template<class Self, size_t I>
using dependencies_t = compute::sequence_t<dependency_indices_v<Self, dependency_t<Self, I>>>;

template<class Self, size_t I, class... Env>
using node_completions_t = decltype(::stdexec::get_completion_signatures<node_sender_t<Self, I>, Env...>());

/// 节点 I 是否消费前驱的值：含输入叶子的节点消费，纯 sender 载荷的节点只有控制依赖。
template<class Self, size_t I>
inline constexpr bool consumes_input_v = compute::input_count_v<child_t<Self, I>> != 0;

/// 每个节点"读它的值的后继"个数（只有控制依赖的后继不算）。
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

/// 节点 D 的值要不要共享：读它的值的后继超过一个才共享（const&），否则 move 给唯一的消费者。
template<class Self, size_t D>
inline constexpr bool shared_v = consumer_counts_v<Self>[D] > 1;

// ---------------------------------------------------------------- 降解用到的小部件

/// 只有控制依赖的节点在降解后是 let_value(fanin(前驱...), constant_sender{载荷})：忽略输入的值，直接给出载荷 sender。
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

/// 节点在某环境下的值完成域；给不出时这个别名替换失败。
template<class Node, class Env>
using node_domain_t = decltype(::stdexec::get_completion_domain<::stdexec::set_value_t>(
	::stdexec::get_env(::std::declval<Node const&>()), ::std::declval<Env const&>()));

}
