#pragma once

/// lowered<Self, FanIn, FanOut>：图降解成 fanin / fanout / let_value 组合后，每个节点的类型。
///
///   input_t<I>         节点 I 的输入：根是 just()，否则 fanin(前驱...)
///   node_sender_t<I>   节点 I 的 sender：输入叶子换成 input_t<I>；只有控制依赖的节点是 let_value(input_t<I>, constant_sender{载荷})
///   built_t<I>         存进构造表的东西：出度 > 1 的节点存 fanout(node_sender_t<I>)
///   storage_t          tuple<optional<built_t<I>>...>
///
/// 后继取前驱的方式：出度 > 1 的前驱已经套过 fanout，后继拿它的 const&（fanin 会拷贝一份）；出度 1 的前驱直接 move。
///
/// 反射实现按拓扑序一次填好三张表（lowered_tables），后面查表。consteval 计算没有编译器缓存，
/// 如果写成"节点类型依赖前驱类型"的互递归函数，计算量随图里的路径数指数增长。
/// 模板实现用类模板，编译器会缓存，按定义写成互相引用即可。

#include <cstddef>
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../../config.h"
#include "../../meta_program/type_list.h"
#include "../../meta_program/utility.h"
#include "../../meta_program/indexed_tuple.h"
#include "../../graph/traits.h"
#include "../../meta_program/reflection.h"
#include "../../meta_program/dependency_indices.h"
#include "../../graph/analyze/sender_expression.h"
#include "../../concurrency/constant_sender.h"

namespace sdag::analyze
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace detail
{

using namespace ::sdag::meta::refl;

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
	constexpr auto shape = graph_detail::shape_of_v<Self>;
	lowered_tables<shape.count> tables;
	infos const keys = arguments(^^typename graph_detail::traits_of<Self>::data::keys);
	infos const dependencies = arguments(^^typename graph_detail::traits_of<Self>::data::dependencies);
	infos const children = arguments(^^typename graph_detail::traits_of<Self>::children);

	auto dependency_input = [&](::std::size_t from)
	{
		return shape.out_degree[from] > 1
			? ::std::meta::add_lvalue_reference(::std::meta::add_const(tables.built[from]))
			: ::std::meta::add_rvalue_reference(tables.built[from]);
	};

	for (::std::size_t index : shape.order)
	{
		infos fanin_arguments{^^FanIn const&};
		for (::std::size_t from : indices_of(arguments(dependencies[index]), keys))
		{
			fanin_arguments.push_back(dependency_input(from));
		}
		tables.input[index] = fanin_arguments.size() == 1
			? ::std::meta::dealias(^^decltype(::stdexec::just()))
			: ::std::meta::dealias(::std::meta::substitute(^^::std::invoke_result_t, fanin_arguments));

		info const child = ::std::meta::substitute(^^meta::copy_cvref_t, {^^Self, children[index]});
		if (analyze::refl::input_count(child) != 0)
		{
			tables.node_sender[index] = analyze::refl::substituted(child, tables.input[index]);
		}
		else if (shape.is_root(index))
		{
			tables.node_sender[index] = plain(child);
		}
		else
		{
			tables.node_sender[index] = ::std::meta::dealias(::std::meta::substitute(^^::std::invoke_result_t,
				{^^::stdexec::let_value_t const&, tables.input[index], ::std::meta::substitute(^^concurrency::constant_sender, {plain(child)})}));
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
struct lowered
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
struct lowered
{
	using size_t = ::std::size_t;
	static constexpr auto shape = graph_detail::shape_of_v<Self>;

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
	using input_t = typename input<I, graph_detail::dependencies_t<Self, I>>::type;

	template<size_t I>
	struct node_sender
	{
		using child = graph_detail::node_sender_t<Self, I>;

		static consteval auto compute() noexcept
		{
			if constexpr (input_count_v<child> != 0)
			{
				return ::std::type_identity<substituted_t<child, input_t<I>>>{};
			}
			else if constexpr (shape.is_root(I))
			{
				return ::std::type_identity<::std::remove_cvref_t<child>>{};
			}
			else
			{
				return ::std::type_identity<::std::invoke_result_t<::stdexec::let_value_t const&, input_t<I>,
					concurrency::constant_sender<::std::remove_cvref_t<child>>>>{};
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

}
