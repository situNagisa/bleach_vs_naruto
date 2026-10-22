#pragma once

/// 降解：把 dag_t sexpr 变成由现成扇入 / 扇出算法（默认 ::stdexec::when_all + ::exec::split）
/// 拼成的普通 sender 表达式树。
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
#include "graph.h"

namespace sdag::composed_detail
{

using ::std::size_t;
using graph_detail::traits_of;
using graph_detail::contains_input_v;

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

// ---------------------------------------------------------------- 把输入叶子换成真正的扇入 sender

using graph_detail::is_input_v;
using graph_detail::substituted_t;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces" // __sexpr 的聚合初始化靠大括号省略，stdexec 自己也这样写
template<class Input, class CvSender>
[[nodiscard]] constexpr substituted_t<CvSender&&, Input> substitute(CvSender&& sender, Input&& input)
{
	using sender_type = ::std::remove_cvref_t<CvSender>;
	if constexpr (is_input_v<sender_type>)
	{
		return ::std::forward<Input>(input);
	}
	else if constexpr (!contains_input_v<sender_type>)
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
#pragma clang diagnostic pop

// ---------------------------------------------------------------- 构造

template<class Self, class FanIn, class FanOut>
struct builder
{
	using traits = traits_of<Self>;
	static constexpr auto shape = traits::shape;
	static constexpr size_t count = traits::count;

	template<size_t I>
	using child_t = graph_detail::node_sender_t<Self, I>;

	template<size_t I>
	using dependencies_t = typename traits::template dependencies_t<I>;

	template<size_t I>
	struct node_sender;

	template<size_t I>
	using node_sender_t = typename node_sender<I>::type;

	/// 出度 > 1 的节点存的是扇出后的 sender。
	template<size_t I>
	using built_t = ::std::conditional_t<(shape.out_degree[I] > 1),
		::std::invoke_result_t<FanOut const&, node_sender_t<I>>,
		node_sender_t<I>>;

	/// 后继取前驱的方式：共享的拷贝一份扇出 sender，独占的直接 move。
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
	using input_t = typename input<I, dependencies_t<I>>::type;

	template<size_t I>
	struct node_sender
	{
		static consteval auto compute() noexcept
		{
			if constexpr (contains_input_v<::std::remove_cvref_t<child_t<I>>>)
			{
				return meta::tag<substituted_t<child_t<I>, input_t<I>>>{};
			}
			else if constexpr (shape.is_root(I))
			{
				return meta::tag<::std::remove_cvref_t<child_t<I>>>{};
			}
			else
			{
				return meta::tag<decltype(::stdexec::let_value(::std::declval<input_t<I>>(),
					::std::declval<constant_sender<::std::remove_cvref_t<child_t<I>>>>()))>{};
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

	using storage_t = typename storage<::std::make_index_sequence<count>>::type;

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
		decltype(auto) sender = ::stdexec::__get<I + 2>(::std::forward<Self>(_graph));
		if constexpr (contains_input_v<::std::remove_cvref_t<child>>)
		{
			return composed_detail::substitute(::std::forward<child>(sender), make_input<I>(dependencies_t<I>{}));
		}
		else if constexpr (shape.is_root(I))
		{
			return ::std::remove_cvref_t<child>(::std::forward<child>(sender));
		}
		else
		{
			return ::stdexec::let_value(make_input<I>(dependencies_t<I>{}),
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

	template<size_t... K>
	[[nodiscard]] auto build(::std::index_sequence<K...>)
	{
		// 按拓扑序构造，保证前驱的 sender 先于后继存在。
		(build_one<shape.order[K]>(), ...);
		return [this]<size_t... S>(::std::index_sequence<S...>)
		{
			if constexpr (sizeof...(S) == 0)
			{
				return ::stdexec::just();
			}
			else
			{
				return _fanin(::std::move(*::std::get<S>(_built))...);
			}
		}(meta::index_sequence_of<graph_detail::sink_indices<shape>>{});
	}
};

}

namespace sdag
{

template<class Graph, class FanIn, class FanOut>
auto lower_t::operator()(Graph&& graph, FanIn const& fanin, FanOut const& fanout) const
{
	static_assert(::exec::sender_for<Graph, dag_t>, "sdag: lower 只接受 dag_t{}(...) / dag(...) 得到的图 sender");
	using builder_type = composed_detail::builder<Graph&&, FanIn, FanOut>;
	builder_type builder{fanin, fanout, ::std::forward<Graph>(graph)};
	return builder.build(::std::make_index_sequence<builder_type::count>{});
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
