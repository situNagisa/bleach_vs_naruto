#pragma once

/// 组合策略：用现成的扇入 / 扇出算法（默认 ::stdexec::when_all + ::exec::split）把图拼成
/// 一棵普通的 sender 表达式树。它是语义参照与可配置入口，不追求 0 开销——split 会堆分配。
///
///   节点 sender = 载荷(扇入(前驱 1 的 sender, 前驱 2 的 sender, ...))
///   出度 > 1 的节点先经过扇出，各后继拿到它的拷贝；出度 1 的节点直接 move 给后继
///   整图 = 扇入(汇点...)
///
/// 值的传递规则与融合策略一致：独占的 move，共享的按 const&（split 的语义）。

#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "meta.h"
#include "node.h"
#include "shape.h"

namespace sdag
{

namespace composed_detail
{

using ::std::size_t;

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

template<class FanIn, class FanOut, class... Nodes>
struct builder
{
	static constexpr auto shape = checked_shape<Nodes...>();
	static constexpr size_t count = sizeof...(Nodes);

	template<size_t I>
	using dependencies_t = dependency_indices_t<I, Nodes...>;

	template<size_t I>
	using payload_t = decltype(::sdag::payload(::std::declval<meta::at_t<I, meta::type_list<Nodes...>>>()));

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
		static_assert(node_payload<payload_t<I>, input_t<I>>,
			"sdag: 节点载荷既不是 sender，也不是能接受输入 sender 的适配器（例如 ::stdexec::then(f)）");

		static consteval auto compute() noexcept
		{
			if constexpr (!sender_payload<payload_t<I>>)
			{
				return meta::tag<::std::invoke_result_t<payload_t<I>, input_t<I>>>{};
			}
			else if constexpr (shape.is_root(I))
			{
				return meta::tag<::std::decay_t<payload_t<I>>>{};
			}
			else
			{
				return meta::tag<decltype(::stdexec::let_value(::std::declval<input_t<I>>(), ::std::declval<constant_sender<::std::decay_t<payload_t<I>>>>()))>{};
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
	::std::tuple<Nodes&&...> _nodes;
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
		decltype(auto) payload = ::sdag::payload(::std::get<I>(::std::move(_nodes)));
		using payload_type = decltype(payload);
		if constexpr (sender_payload<payload_type>)
		{
			if constexpr (shape.is_root(I))
			{
				return ::std::decay_t<payload_type>(::std::forward<payload_type>(payload));
			}
			else
			{
				return ::stdexec::let_value(make_input<I>(dependencies_t<I>{}),
					constant_sender<::std::decay_t<payload_type>>{::std::forward<payload_type>(payload)});
			}
		}
		else
		{
			return ::std::forward<payload_type>(payload)(make_input<I>(dependencies_t<I>{}));
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
		}(meta::index_sequence_of<meta::filtered_indices<count, [](size_t index) { return shape.is_sink(index); }>>{});
	}
};

}

/// 组合策略：fanin 接收 1 个或多个 sender（当只有 1 个时应原样返回），fanout 接收 1 个 sender 并返回可拷贝的 sender。
template<class FanIn = ::stdexec::when_all_t, class FanOut = ::exec::split_t>
struct composed
{
	[[no_unique_address]] FanIn _fanin{};
	[[no_unique_address]] FanOut _fanout{};

	template<graph_node... Nodes>
	[[nodiscard]] auto compose(Nodes&&... nodes) const
	{
		using builder_type = composed_detail::builder<FanIn, FanOut, Nodes...>;
		builder_type builder{_fanin, _fanout, ::std::tuple<Nodes&&...>(::std::forward<Nodes>(nodes)...)};
		return builder.build(::std::make_index_sequence<sizeof...(Nodes)>{});
	}
};

}
