#pragma once

/// 降解：把图 sender 改写成由现成的扇入 / 扇出算法（默认 when_all + split）拼成的普通 sender 树。
///
///   节点 sender = 节点原来的 sender，其中的占位符换成 fanin(前驱 1, 前驱 2, ...)
///   出度 > 1 的节点先套 fanout，各后继拿到它的一份拷贝；出度 1 的节点直接 move 给唯一的后继
///   只有控制依赖的节点 = let_value(fanin(前驱...), constant_sender{载荷})
///   整图 = fanin(汇点...)
///
/// 用途有两个：domain 定制了 when_all 时 dag_t::transform_sender 自动走这里，让 domain 看到它认识的标准算法；
/// composed 策略则无条件走这里，作为语义参照与"扇入扇出可配置"的入口。
/// 这条路不追求 0 开销：split 必须堆分配共享状态，这是组合现成算法的固有代价。
///
/// 构造顺序：节点按拓扑序逐个构造，前驱的 sender 已经在 _built 里了，后继再把它取走（独占）或拷贝（共享）。
/// 所有类型由下面的 lowered_types 在编译期算好，这里只做运行期的构造。

#include <cstddef>
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/sender_for.hpp>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/cvref.h"
#include "../general/sequence.h"
#include "../general/digraph.h"
#include "../stdexec/expressions.h"
#include "../protocol/hole.h"
#include "../node/node_graph.h"
#include "../graph/description.h"

namespace sdag::execution
{

/// constant_sender<Sender>：忽略参数、交出一个事先存好的 sender 的可调用对象。
/// let_value(fanin(前驱...), constant_sender{载荷}) 等前驱完成后丢掉它们的值，再启动载荷——只有控制依赖的节点就这样降解。
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

/// lowered_types<Self, FanIn, FanOut>：图降解后每个节点的类型。
///
///   input_t<I>         节点 I 的输入：根是 just()，否则 fanin(前驱...)
///   node_sender_t<I>   节点 I 的 sender：占位符换成 input_t<I>；只有控制依赖的节点是 let_value(input_t<I>, constant_sender{载荷})
///   built_t<I>         存进构造表的东西：出度 > 1 的节点存 fanout(node_sender_t<I>)
///   storage_t          tuple<optional<built_t<I>>...>
///
/// 反射实现按拓扑序一次填好三张表（lowered_tables），后面查表。consteval 计算没有编译器缓存，
/// 如果写成"节点类型依赖前驱类型"的互递归函数，计算量随图里的路径数指数增长。
/// 模板实现用类模板，编译器会缓存，按定义写成互相引用即可。
inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace detail
{

using namespace ::sdag::general::refl;

template<::std::size_t N>
struct lowered_tables
{
	info_array<N> input{};
	info_array<N> node_sender{};
	info_array<N> built{};
};

template<class Self, class FanIn, class FanOut>
[[nodiscard]] consteval auto lower_types()
{
	constexpr auto shape = graph::shape_of_v<Self>;
	lowered_tables<shape.count> tables;
	infos const keys = arguments(^^typename graph::description_of<Self>::data_type::keys);
	infos const dependencies = arguments(^^typename graph::description_of<Self>::data_type::dependencies);
	infos const children = arguments(^^typename graph::description_of<Self>::children);

	auto dependency_input = [&](::std::size_t from)
	{
		return shape.out_degree[from] > 1
			? ::std::meta::add_lvalue_reference(::std::meta::add_const(tables.built[from]))
			: ::std::meta::add_rvalue_reference(tables.built[from]);
	};

	for (::std::size_t index : shape.order)
	{
		infos fanin_arguments{^^FanIn const&};
		for (::std::size_t from : ::sdag::general::refl::indices_of(arguments(dependencies[index]), keys))
		{
			fanin_arguments.push_back(dependency_input(from));
		}
		tables.input[index] = fanin_arguments.size() == 1
			? ::std::meta::dealias(^^decltype(::stdexec::just()))
			: ::std::meta::dealias(::std::meta::substitute(^^::std::invoke_result_t, fanin_arguments));

		info const child = ::std::meta::substitute(^^general::copy_cvref_t, {^^Self, children[index]});
		if (protocol::refl::hole_count(child) != 0)
		{
			tables.node_sender[index] = protocol::refl::hole_filled(child, tables.input[index]);
		}
		else if (shape.is_root(index))
		{
			tables.node_sender[index] = plain(child);
		}
		else
		{
			tables.node_sender[index] = ::std::meta::dealias(::std::meta::substitute(^^::std::invoke_result_t,
				{^^::stdexec::let_value_t const&, tables.input[index], ::std::meta::substitute(^^execution::constant_sender, {plain(child)})}));
		}

		tables.built[index] = shape.out_degree[index] > 1
			? ::std::meta::dealias(::std::meta::substitute(^^::std::invoke_result_t, {^^FanOut const&, tables.node_sender[index]}))
			: tables.node_sender[index];
	}
	return tables;
}

template<::std::size_t N>
[[nodiscard]] consteval info storage_type(lowered_tables<N> const& tables)
{
	infos slots;
	for (::std::size_t index = 0; index < N; ++index)
	{
		slots.push_back(::std::meta::substitute(^^::std::optional, {tables.built[index]}));
	}
	return ::std::meta::substitute(^^::std::tuple, slots);
}

}

template<class Self, class FanIn, class FanOut>
struct lowered_types
{
	static constexpr auto tables = detail::lower_types<Self, FanIn, FanOut>();

	template<::std::size_t I>
	using input_t = typename [: tables.input[I] :];

	template<::std::size_t I>
	using node_sender_t = typename [: tables.node_sender[I] :];

	template<::std::size_t I>
	using built_t = typename [: tables.built[I] :];

	using storage_t = typename [: detail::storage_type(tables) :];
};

#else

template<class Self, class FanIn, class FanOut>
struct lowered_types
{
	using size_t = ::std::size_t;
	static constexpr auto shape = graph::shape_of_v<Self>;

	template<size_t I>
	struct node_sender;

	template<size_t I>
	using node_sender_t = typename node_sender<I>::type;

	template<size_t I>
	using built_t = ::std::conditional_t<(shape.out_degree[I] > 1),
		::std::invoke_result_t<FanOut const&, node_sender_t<I>>,
		node_sender_t<I>>;

	template<size_t D>
	using dependency_input_t = ::std::conditional_t<(shape.out_degree[D] > 1), built_t<D> const&, built_t<D>&&>;

	template<size_t I, class Dependencies>
	struct input;

	template<size_t I>
	struct input<I, ::std::index_sequence<>>
	{
		using type = decltype(::stdexec::just());
	};

	template<size_t I, size_t... D>
		requires (sizeof...(D) > 0)
	struct input<I, ::std::index_sequence<D...>>
	{
		using type = ::std::invoke_result_t<FanIn const&, dependency_input_t<D>...>;
	};

	template<size_t I>
	using input_t = typename input<I, graph::dependencies_t<Self, I>>::type;

	template<size_t I>
	struct node_sender
	{
		using child = graph::node_sender_t<Self, I>;

		static consteval auto compute() noexcept
		{
			if constexpr (protocol::hole_count_v<child> != 0)
			{
				return ::std::type_identity<protocol::hole_filled_t<child, input_t<I>>>{};
			}
			else if constexpr (shape.is_root(I))
			{
				return ::std::type_identity<::std::remove_cvref_t<child>>{};
			}
			else
			{
				return ::std::type_identity<::std::invoke_result_t<::stdexec::let_value_t const&, input_t<I>,
					execution::constant_sender<::std::remove_cvref_t<child>>>>{};
			}
		}

		using type = typename decltype(compute())::type;
	};

	template<class Indices>
	struct storage;

	template<size_t... I>
	struct storage<::std::index_sequence<I...>>
	{
		using type = ::std::tuple<::std::optional<built_t<I>>...>;
	};

	using storage_t = typename storage<::std::make_index_sequence<shape.count>>::type;
};

#endif

}

namespace lowering_detail
{

using ::std::size_t;

template<class Self, class FanIn, class FanOut>
struct builder
{
	using types = lowered_types<Self, FanIn, FanOut>;
	static constexpr auto shape = graph::shape_of_v<Self>;

	template<size_t I>
	using child_t = graph::node_sender_t<Self, I>;

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
	[[nodiscard]] constexpr dependency_input_t<D> take_dependency() noexcept
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
	[[nodiscard]] constexpr input_t<I> make_input(::std::index_sequence<D...>)
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
	[[nodiscard]] constexpr node_sender_t<I> make_node_sender()
	{
		using child = child_t<I>;
		using dependencies = graph::dependencies_t<Self, I>;
		decltype(auto) sender = ::std::forward<Self>(_graph).template get<I + 2>();
		if constexpr (protocol::hole_count_v<child> != 0)
		{
			// 数据依赖：占位符换成 fanin(前驱...)。
			return protocol::fill_hole(::std::forward<child>(sender), make_input<I>(dependencies{}));
		}
		else if constexpr (shape.is_root(I))
		{
			return ::std::remove_cvref_t<child>(::std::forward<child>(sender));
		}
		else
		{
			// 控制依赖：等前驱都结束，丢掉它们的值，再启动载荷 sender。
			return ::stdexec::let_value(make_input<I>(dependencies{}),
				execution::constant_sender<::std::remove_cvref_t<child>>{::std::forward<child>(sender)});
		}
	}

	template<size_t I>
	constexpr void build_one()
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
	[[nodiscard]] constexpr auto join_sinks(::std::index_sequence<S...>)
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

	[[nodiscard]] constexpr auto build()
	{
		general::for_each_value<shape.order>([this]<size_t I>()
		{
			this->template build_one<I>();
		});
		return join_sinks(general::sequence_t<general::sinks_v<shape>>{});
	}
};

}

/// lower(graph, fanin, fanout)：把图 sender 降解为 fanin / fanout / let_value 组合的普通 sender 树。
/// fanin 接收 1 个或多个 sender（只有 1 个时应原样返回，when_all 就是这样），fanout 接收 1 个 sender 并返回可拷贝的 sender。
struct lower_t
{
	template<class Graph, class FanIn, class FanOut>
	[[nodiscard]] constexpr auto operator()(Graph&& graph, FanIn const& fanin, FanOut const& fanout) const
	{
		static_assert(requires { typename graph::description_of<Graph>; }, "sdag: lower 只接受 dag(...) 得到的图 sender");
		lowering_detail::builder<Graph&&, FanIn, FanOut> builder{fanin, fanout, ::std::forward<Graph>(graph)};
		return builder.build();
	}
};

inline constexpr lower_t lower{};

}
