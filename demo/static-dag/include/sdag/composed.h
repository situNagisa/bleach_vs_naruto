#pragma once

/// 降解：把图 sender 变成由现成扇入 / 扇出算法（默认 ::stdexec::when_all + ::exec::split）
/// 拼成的普通 sender 表达式树。类型由编译期计算层算好（compute::lowered），这里只做构造。
///
///   节点 sender = 节点原来的 sender，其中的输入叶子换成 fanin(前驱 1, 前驱 2, ...)
///   出度 > 1 的节点先经过 fanout，各后继拿到它的拷贝；出度 1 的节点直接 move 给后继
///   只有控制依赖的节点 = let_value(fanin(前驱...), 常量 sender)
///   整图 = fanin(汇点...)
///
/// 两个用途：所在 domain 定制了 when_all 时 dag_t::transform_sender 自动走这里，让 domain 看到
/// 它认识的标准算法；composed 策略则无条件走这里，作为语义参照与可配置入口。
/// 它不追求 0 开销——split 会堆分配，这是组合现成 combinator 的固有代价。

#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/sender_for.hpp>
#include <exec/split.hpp>

#include "meta.h"
#include "node.h"
#include "shape.h"
#include "stdexec_bridge.h"
#include "graph_sender.h"
#include "graph.h"
#include "compute/primitives.h"
#include "compute/analysis.h"

namespace sdag::composed_detail
{

using ::std::size_t;

template<class T>
inline constexpr bool is_input_v = false;

template<class Dependency>
inline constexpr bool is_input_v<input_sender<Dependency>> = true;

/// 把输入叶子换成真正的扇入 sender：沿 sender 表达式拆开、替换、重建。
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
				// 只有一个子树含输入叶子，input 只会被真正移动一次。
				return bridge::make_expression(::std::forward<Tag>(tag), ::std::forward<Data>(data),
					composed_detail::substitute(::std::forward<Children>(children), ::std::forward<Input>(input))...);
			},
			::std::forward<CvSender>(sender));
	}
}

template<class Self, class FanIn, class FanOut>
struct builder
{
	using types = compute::lowered<Self, FanIn, FanOut>;
	static constexpr auto shape = graph_detail::shape_of_v<Self>;
	static constexpr size_t count = shape.count;

	template<size_t I>
	using child_t = graph_detail::node_sender_t<Self, I>;

	template<size_t I>
	using node_sender_t = typename types::template node_sender_t<I>;

	template<size_t I>
	using input_t = typename types::template input_t<I>;

	/// 后继取前驱的方式：共享的拷贝一份扇出 sender，独占的直接 move。
	template<size_t D>
	using dependency_input_t = ::std::conditional_t<(shape.out_degree[D] > 1),
		typename types::template built_t<D> const&,
		typename types::template built_t<D>&&>;

	using storage_t = typename types::storage_t;

	FanIn const& _fanin;
	FanOut const& _fanout;
	Self _graph;
	storage_t _built{};

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
			return composed_detail::substitute(::std::forward<child>(sender), make_input<I>(dependencies{}));
		}
		else if constexpr (shape.is_root(I))
		{
			return ::std::remove_cvref_t<child>(::std::forward<child>(sender));
		}
		else
		{
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
		// 按拓扑序构造，保证前驱的 sender 先于后继存在。
		compute::for_each_value<shape.order>([this]<size_t I>()
		{
			this->template build_one<I>();
		});
		return join_sinks(compute::sequence_t<sinks_v<shape>>{});
	}
};

}

namespace sdag
{

template<class Graph, class FanIn, class FanOut>
auto lower_t::operator()(Graph&& graph, FanIn const& fanin, FanOut const& fanout) const
{
	static_assert(::exec::sender_for<Graph, dag_t>, "sdag: lower 只接受 dag_t{}(...) / dag(...) 得到的图 sender");
	composed_detail::builder<Graph&&, FanIn, FanOut> builder{fanin, fanout, ::std::forward<Graph>(graph)};
	return builder.build();
}

/// 组合策略：fanin 接收 1 个或多个 sender（只有 1 个时应原样返回），fanout 接收 1 个 sender 并返回可拷贝的 sender。
template<class FanIn = ::stdexec::when_all_t, class FanOut = ::exec::split_t>
struct composed
{
	SDAG_NO_UNIQUE_ADDRESS FanIn _fanin{};
	SDAG_NO_UNIQUE_ADDRESS FanOut _fanout{};

	template<graph_node... Nodes>
	[[nodiscard]] auto compose(Nodes&&... nodes) const
	{
		return ::sdag::lower(dag_t{}(::std::forward<Nodes>(nodes)...), _fanin, _fanout);
	}
};

}
