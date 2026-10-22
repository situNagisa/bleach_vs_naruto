#pragma once

/// 降解：把 dag_t sexpr 变成由现成扇入 / 扇出算法（默认 ::stdexec::when_all + ::exec::split）
/// 拼成的普通 sender 表达式树。类型全部用反射算出，运行期只做构造。
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
#include <meta>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>
#include <exec/sender_for.hpp>
#include <exec/split.hpp>

#include "meta.h"
#include "node.h"
#include "shape.h"
#include "graph.h"

namespace sdag::composed_detail
{

using ::std::size_t;
using meta::info;
using meta::infos;
using graph_detail::input_count;
using graph_detail::is_input;
using graph_detail::substituted;
using graph_detail::substituted_t;

/// 只有控制依赖的节点：等输入完成后忽略它的值，直接启动载荷 sender。
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

// ---------------------------------------------------------------- 把输入叶子换成真正的扇入 sender（运行期）

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces" // __sexpr 的聚合初始化靠大括号省略，stdexec 自己也这样写
template<class Input, class CvSender>
[[nodiscard]] constexpr substituted_t<CvSender&&, Input> substitute(CvSender&& sender, Input&& input)
{
	using sender_type = ::std::remove_cvref_t<CvSender>;
	if constexpr (is_input(^^sender_type))
	{
		return ::std::forward<Input>(input);
	}
	else if constexpr (input_count(^^sender_type) == 0)
	{
		return ::std::forward<CvSender>(sender);
	}
	else
	{
		return ::stdexec::__apply(
			[&]<class Tag, class Data, class... Children>(Tag tag, Data&& data, Children&&... children)
			{
				// 只有一个子树含输入叶子，input 只会被真正移动一次。
				return ::stdexec::__sexpr{tag, ::std::decay_t<Data>(::std::forward<Data>(data)),
					composed_detail::substitute(::std::forward<Children>(children), ::std::forward<Input>(input))...};
			},
			::std::forward<CvSender>(sender));
	}
}
#pragma GCC diagnostic pop

// ---------------------------------------------------------------- 降解后的类型（反射）

/// 每个节点降解后的三种类型；consteval 计算没有缓存，所以按拓扑序一次算完存成表，不做互递归。
template<size_t N>
struct lowered_tables
{
	/// 节点的输入：根是 just()，否则 fanin(前驱...)。
	graph_detail::info_array<N> input{};
	/// 节点 sender：输入叶子换成 input 之后的 sender；只有控制依赖的节点是 let_value(input, 常量 sender)。
	graph_detail::info_array<N> node_sender{};
	/// 存进构造表里的东西：出度 > 1 的节点存扇出后的 sender。
	graph_detail::info_array<N> built{};
};

template<class Self, class FanIn, class FanOut>
[[nodiscard]] consteval auto lower_types()
{
	constexpr auto shape = graph_detail::shape_of_v<Self>;
	lowered_tables<shape.count> tables;
	infos const dependencies = meta::arguments(^^typename graph_detail::data_t<Self>::dependencies);
	infos const children = graph_detail::children_of(^^Self);

	// 后继取前驱的方式：共享的拷贝一份扇出 sender，独占的直接 move。
	auto dependency_input = [&](size_t from)
	{
		return shape.out_degree[from] > 1
			? ::std::meta::add_lvalue_reference(::std::meta::add_const(tables.built[from]))
			: ::std::meta::add_rvalue_reference(tables.built[from]);
	};

	for (size_t index : shape.order)
	{
		infos fanin_arguments{^^FanIn const&};
		for (size_t from : graph_detail::dependency_indices<Self>(dependencies[index]))
		{
			fanin_arguments.push_back(dependency_input(from));
		}
		tables.input[index] = fanin_arguments.size() == 1
			? ^^decltype(::stdexec::just())
			: ::std::meta::dealias(::std::meta::substitute(^^::std::invoke_result_t, fanin_arguments));

		info const child = ::std::meta::substitute(^^::stdexec::__copy_cvref_t, {^^Self, children[index]});
		if (input_count(child) != 0)
		{
			tables.node_sender[index] = substituted(child, tables.input[index]);
		}
		else if (shape.is_root(index))
		{
			tables.node_sender[index] = graph_detail::plain(child);
		}
		else
		{
			tables.node_sender[index] = ::std::meta::dealias(::std::meta::substitute(^^::std::invoke_result_t,
				{^^::stdexec::let_value_t const&, tables.input[index], ::std::meta::substitute(^^constant_sender, {graph_detail::plain(child)})}));
		}

		tables.built[index] = shape.out_degree[index] > 1
			? ::std::meta::dealias(::std::meta::substitute(^^::std::invoke_result_t, {^^FanOut const&, tables.node_sender[index]}))
			: tables.node_sender[index];
	}
	return tables;
}

/// std::tuple<std::optional<built...>...>
template<size_t N>
[[nodiscard]] consteval info storage_type(lowered_tables<N> const& tables)
{
	infos slots;
	for (size_t index = 0; index < N; ++index)
	{
		slots.push_back(::std::meta::substitute(^^::std::optional, {tables.built[index]}));
	}
	return ::std::meta::substitute(^^::std::tuple, slots);
}

// ---------------------------------------------------------------- 构造

template<class Self, class FanIn, class FanOut>
struct builder
{
	static constexpr auto shape = graph_detail::shape_of_v<Self>;
	static constexpr size_t count = shape.count;
	static constexpr auto tables = composed_detail::lower_types<Self, FanIn, FanOut>();

	template<size_t I>
	using child_t = graph_detail::node_sender_t<Self, I>;

	template<size_t I>
	using node_sender_t = typename [: tables.node_sender[I] :];

	template<size_t I>
	using input_t = typename [: tables.input[I] :];

	template<size_t D>
	using dependency_input_t = ::std::conditional_t<(shape.out_degree[D] > 1),
		typename [: tables.built[D] :] const&,
		typename [: tables.built[D] :]&&>;

	using storage_t = typename [: composed_detail::storage_type(tables) :];

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
		decltype(auto) sender = ::stdexec::__get<I + 2>(::std::forward<Self>(_graph));
		if constexpr (input_count(^^child) != 0)
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
				constant_sender<::std::remove_cvref_t<child>>{::std::forward<child>(sender)});
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
		template for (constexpr size_t index : shape.order)
		{
			build_one<index>();
		}
		return join_sinks(sink_sequence<shape>{});
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
