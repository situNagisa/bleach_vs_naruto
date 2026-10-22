#pragma once

/// 降解：把图 sender 改写成由现成的扇入 / 扇出算法（默认 when_all + split）拼成的普通 sender 树。
///
///   节点 sender = 节点原来的 sender，其中的输入叶子换成 fanin(前驱 1, 前驱 2, ...)
///   出度 > 1 的节点先套 fanout，各后继拿到它的一份拷贝；出度 1 的节点直接 move 给唯一的后继
///   只有控制依赖的节点 = let_value(fanin(前驱...), constant_sender{载荷})
///   整图 = fanin(汇点...)
///
/// 用途有两个：domain 定制了 when_all 时 dag_t::transform_sender 自动走这里，让 domain 看到它认识的标准算法；
/// composed 策略则无条件走这里，作为语义参照与"扇入扇出可配置"的入口。
/// 这条路不追求 0 开销：split 必须堆分配共享状态，这是组合现成算法的固有代价。
///
/// 构造顺序：节点按拓扑序逐个构造，前驱的 sender 已经在 _built 里了，后继再把它取走（独占）或拷贝（共享）。
/// 所有类型由编译期计算层的 compute::lowered 预先算好，这里只做运行期的构造。

#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/sender_for.hpp>

#include "meta.h"
#include "fwd.h"
#include "stdexec_bridge.h"
#include "graph_traits.h"
#include "compute/primitives.h"
#include "compute/analysis.h"

namespace sdag
{

namespace lowering_detail
{

using ::std::size_t;

template<class T>
inline constexpr bool is_input_v = false;

template<class Dependency>
inline constexpr bool is_input_v<input_sender<Dependency>> = true;

/// 把节点 sender 里的输入叶子换成 input：沿 stdexec 的算法层拆开、替换、重建。
/// 类型与 compute::substituted_t 算出的一致；不含输入叶子的子树原样转发。
template<class Input, class CvSender>
[[nodiscard]] constexpr compute::substituted_t<CvSender&&, Input> substitute(CvSender&& sender, Input&& input)
{
	using sender_type = ::std::remove_cvref_t<CvSender>;
	if constexpr (is_input_v<sender_type>)
	{
		return ::std::forward<Input>(input);
	}
	else if constexpr (compute::input_count_v<sender_type> == 0)
	{
		return ::std::forward<CvSender>(sender);
	}
	else
	{
		return bridge::apply(
			[&]<class Tag, class Data, class... Children>(Tag&& tag, Data&& data, Children&&... children)
			{
				// 节点只含一个输入叶子，所以只有一个子树会真正用到 input，它只会被移动一次。
				return bridge::make_expression(::std::forward<Tag>(tag), ::std::forward<Data>(data),
					lowering_detail::substitute(::std::forward<Children>(children), ::std::forward<Input>(input))...);
			},
			::std::forward<CvSender>(sender));
	}
}

template<class Self, class FanIn, class FanOut>
struct builder
{
	using types = compute::lowered<Self, FanIn, FanOut>;
	static constexpr auto shape = graph_detail::shape_of_v<Self>;

	template<size_t I>
	using child_t = graph_detail::node_sender_t<Self, I>;

	template<size_t I>
	using node_sender_t = typename types::template node_sender_t<I>;

	template<size_t I>
	using input_t = typename types::template input_t<I>;

	/// 后继取前驱的方式：共享的（出度 > 1，已经套过 fanout）拷贝一份，独占的直接 move。
	template<size_t D>
	using dependency_input_t = ::std::conditional_t<(shape.out_degree[D] > 1),
		typename types::template built_t<D> const&,
		typename types::template built_t<D>&&>;

	FanIn const& _fanin;
	FanOut const& _fanout;
	Self _graph;
	/// 每个节点构造好的 sender（出度 > 1 时是套过 fanout 的）。
	typename types::storage_t _built{};

	template<size_t D>
	[[nodiscard]] dependency_input_t<D> take_dependency() noexcept
	{
		if constexpr (shape.out_degree[D] > 1)
		{
			return *::std::get<D>(_built);
		}
		else
		{
			return ::std::move(*::std::get<D>(_built));
		}
	}

	template<size_t I, size_t... D>
	[[nodiscard]] input_t<I> make_input(::std::index_sequence<D...>)
	{
		if constexpr (sizeof...(D) == 0)
		{
			return ::stdexec::just();
		}
		else
		{
			return _fanin(take_dependency<D>()...);
		}
	}

	template<size_t I>
	[[nodiscard]] node_sender_t<I> make_node_sender()
	{
		using child = child_t<I>;
		using dependencies = graph_detail::dependencies_t<Self, I>;
		decltype(auto) sender = ::std::forward<Self>(_graph).template get<I + 2>();
		if constexpr (compute::input_count_v<child> != 0)
		{
			// 数据依赖：输入叶子换成 fanin(前驱...)。
			return lowering_detail::substitute(::std::forward<child>(sender), make_input<I>(dependencies{}));
		}
		else if constexpr (shape.is_root(I))
		{
			return ::std::remove_cvref_t<child>(::std::forward<child>(sender));
		}
		else
		{
			// 控制依赖：等前驱都结束，丢掉它们的值，再启动载荷 sender。
			return ::stdexec::let_value(make_input<I>(dependencies{}),
				graph_detail::constant_sender<::std::remove_cvref_t<child>>{::std::forward<child>(sender)});
		}
	}

	template<size_t I>
	void build_one()
	{
		if constexpr (shape.out_degree[I] > 1)
		{
			::std::get<I>(_built).emplace(_fanout(make_node_sender<I>()));
		}
		else
		{
			::std::get<I>(_built).emplace(make_node_sender<I>());
		}
	}

	template<size_t... S>
	[[nodiscard]] auto join_sinks(::std::index_sequence<S...>)
	{
		if constexpr (sizeof...(S) == 0)
		{
			return ::stdexec::just();
		}
		else
		{
			return _fanin(::std::move(*::std::get<S>(_built))...);
		}
	}

	[[nodiscard]] auto build()
	{
		compute::for_each_value<shape.order>([this]<size_t I>()
		{
			this->template build_one<I>();
		});
		return join_sinks(compute::sequence_t<sinks_v<shape>>{});
	}
};

}

/// lower(graph, fanin, fanout)：把图 sender 降解为 fanin / fanout / let_value 组合的普通 sender 树。
/// fanin 接收 1 个或多个 sender（只有 1 个时应原样返回，when_all 就是这样），fanout 接收 1 个 sender 并返回可拷贝的 sender。
struct lower_t
{
	template<class Graph, class FanIn, class FanOut>
	[[nodiscard]] auto operator()(Graph&& graph, FanIn const& fanin, FanOut const& fanout) const
	{
		static_assert(::exec::sender_for<Graph, dag_t>, "sdag: lower 只接受 dag(...) 得到的图 sender");
		lowering_detail::builder<Graph&&, FanIn, FanOut> builder{fanin, fanout, ::std::forward<Graph>(graph)};
		return builder.build();
	}
};

inline constexpr lower_t lower{};

}
