#pragma once

/// 值槽与交付：前驱的值存哪里、怎么按序交给读者；实现取值源的「交付接口」。
///
///   graph_state<Self, Env>   整图 op-state 里存值的那一层，也是这张图的取值源：
///                            签名接口由 protocol::value_source_traits 的特化转到 pred_completions_t（graph/pred_sigs.h），交付接口是这里的 deliver。
///                            融合 op-state 从它派生；节点环境里只暴露这一层的指针。
///   slot_access              交付表的一项：从哪个节点的槽位取第几个值，move（take）还是给 const&
///   delivery_plan<...>(…)    把一组节点的值按顺序铺平成一张交付表
///
/// 为什么要交付表：一个节点可能依赖多个前驱，每个前驱又有多个值，最后要拼成一次 set_value(值...)。
/// 直接写就是"带着已经取出的值一层层递归"。这里先在编译期把每个实参的来源算成一张平表，
/// 运行期只需要一次包展开 set_value(receiver, access<table[E]>()...)。

#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/tuple.h"
#include "../general/relay.h"
#include "../protocol/value_source.h"
#include "description.h"
#include "readers.h"
#include "analysis.h"
#include "storage_plan.h"
#include "pred_sigs.h"
#include "visibility.h"

/// 整图状态的签名接口在类外提供：节点签名算出来之前 graph_state 还是不完整类型（见 protocol/value_source.h）。
namespace sdag::graph
{

template<class Self, class Env>
struct graph_state;

}

template<class Self, class Env>
struct sdag::protocol::value_source_traits<sdag::graph::graph_state<Self, Env>>
{
	template<class Dependency, class NodeEnv>
	using completions_for = sdag::graph::pred_completions_t<Self, Dependency, NodeEnv>;
};

namespace sdag::graph
{

namespace slots_detail
{

template<class Self, class Env, class Indices = ::std::make_index_sequence<count_of_v<Self>>>
struct slots;

/// 值槽按 storage_plan::layout 的链存放：每条链一块存储，放在链头节点的位置上，其它位置是 0 字节的 empty。
///   链只有一个节点（没有复用）：optional<tuple<衰变后的值...>>
///   链有多个节点：relay<各节点的 tuple...>（general/relay.h），按链里的先后轮流占用
/// 不需要存值的节点（没人读、或唯一汇点直接转发）不在任何链里。
template<class Self, class Env, size_t... I>
struct slots<Self, Env, ::std::index_sequence<I...>>
{
	using analysis = node_analysis<Self, Env>;
	using plan = storage_plan<Self, Env>;
	static constexpr auto layout = plan::layout;

	template<size_t J>
	using value_t = general::rebind_t<::std::tuple, typename analysis::template values_t<J>>;

	/// 链头 H 的链上的节点，按先后。
	template<size_t H>
	static constexpr auto chain_v = []
	{
		::std::array<size_t, layout.size[H]> members{};
		for (size_t node = 0; node < sizeof...(I); ++node)
		{
			if (plan::slot_needed[node] && layout.head[node] == H)
			{
				members[layout.position[node]] = node;
			}
		}
		return members;
	}();

	template<size_t H>
	static consteval auto storage_of()
	{
		if constexpr (!plan::slot_needed[H] || layout.head[H] != H)
		{
			return ::std::type_identity<general::empty>{};
		}
		else if constexpr (layout.size[H] == 1)
		{
			return ::std::type_identity<::std::optional<value_t<H>>>{};
		}
		else
		{
			return []<size_t... K>(::std::index_sequence<K...>)
			{
				return ::std::type_identity<general::relay<value_t<chain_v<H>[K]>...>>{};
			}(::std::make_index_sequence<layout.size[H]>{});
		}
	}

	using type = general::indexed_tuple<typename decltype(storage_of<I>())::type...>;
};

}

struct slot_access
{
	size_t node;
	size_t element;
	bool move;
};

/// Nodes 的值按顺序铺平成一张交付表；Moved[i] 为真的节点 move 出去，否则给 const&。
template<class Self, class Env, auto Nodes, auto Moved>
[[nodiscard]] consteval auto delivery_plan()
{
	constexpr auto counts = []<size_t... I>(::std::index_sequence<I...>)
	{
		return ::std::array<size_t, sizeof...(I)>{node_analysis<Self, Env>::template values_t<I>::size...};
	}(::std::make_index_sequence<count_of_v<Self>>{});
	constexpr size_t total = [&]
	{
		size_t sum = 0;
		for (size_t node : Nodes)
		{
			sum += counts[node];
		}
		return sum;
	}();
	::std::array<slot_access, total> plan{};
	size_t cursor = 0;
	for (size_t position = 0; position < Nodes.size(); ++position)
	{
		for (size_t element = 0; element < counts[Nodes[position]]; ++element)
		{
			plan[cursor++] = {Nodes[position], element, Moved[position]};
		}
	}
	return plan;
}

template<class Self, class Env>
struct graph_state
{
	/// 节点环境里的取值源就是整图状态本身。
	using view = node_view<graph::graph_state, Self, Env>;
	using env_type = typename view::env_type;
	using analysis_type = typename view::analysis;
	using plan_type = typename view::plan;

	static_assert(view::valid, "sdag: 有节点在接收者环境（或整图的节点环境）下无法给出完成签名");

	static constexpr auto shape = shape_of_v<Self>;
	static constexpr bool stoppable = view::stoppable;

	SDAG_NO_UNIQUE_ADDRESS typename slots_detail::slots<Self, env_type>::type _slots{};

	static constexpr auto layout = plan_type::layout;

	/// 节点 J 的值所在的存储（链头的那一块），以及在其中的位置。
	template<size_t J>
	[[nodiscard]] constexpr auto& storage_of() noexcept
	{
		return general::get<layout.head[J]>(_slots);
	}

	/// 节点 J 的值（调用方保证它在世）。
	template<size_t J>
	[[nodiscard]] constexpr auto& slot_value() noexcept
	{
		if constexpr (layout.size[J] == 1)
		{
			return *storage_of<J>();
		}
		else
		{
			return storage_of<J>().template get<layout.position[J]>();
		}
	}

	/// 把节点 J 的值存进它的槽位。
	template<size_t J, class... Args>
	constexpr void slot_emplace(Args&&... args)
		noexcept(::std::is_nothrow_constructible_v<typename slots_detail::slots<Self, env_type>::template value_t<J>, Args&&...>)
	{
		if constexpr (layout.size[J] == 1)
		{
			storage_of<J>().emplace(::std::forward<Args>(args)...);
		}
		else
		{
			storage_of<J>().template emplace<layout.position[J]>(::std::forward<Args>(args)...);
		}
	}

	/// 析构节点 J 的值（它被 take 了，取走它的节点已经结束）；之后这块存储可以给链上的下一个值用。
	template<size_t J>
	constexpr void slot_reset() noexcept
	{
		if constexpr (layout.size[J] == 1)
		{
			if constexpr (!::std::is_trivially_destructible_v<typename slots_detail::slots<Self, env_type>::template value_t<J>>)
			{
				storage_of<J>().reset();
			}
		}
		else
		{
			storage_of<J>().template reset<layout.position[J]>();
		}
	}

	/// 按交付表的一项取值：take 的 move 出去，其余给 const&。
	template<slot_access Access>
	[[nodiscard]] constexpr decltype(auto) access() noexcept
	{
		auto& value = ::std::get<Access.element>(slot_value<Access.node>());
		if constexpr (Access.move)
		{
			return ::std::move(value);
		}
		else
		{
			return ::std::as_const(value);
		}
	}

	/// Dependency（data<...>）对应前驱的值，按声明顺序铺平。
	template<class Dependency>
	static constexpr auto delivery_table = graph::delivery_plan<Self, env_type, dependency_indices_v<Self, Dependency>, Dependency::taken>();

	/// 交付接口：占位符启动时调用，把 Dependency 对应前驱的值交给接收者。
	template<class Dependency, class Receiver>
	constexpr void deliver(Receiver&& receiver) noexcept
	{
		// 接收者那一侧是在它自己的环境（node_env 外面可能又包了几层适配器）下算出占位符签名的，
		// 这里是在整图环境下存的值。两边必须一致，否则说明某个节点 sender 在不同环境下报告了不同的值类型。
		using declared = pred_completions_t<Self, Dependency, ::stdexec::env_of_t<Receiver>>;
		using stored = pred_completions_t<Self, Dependency, env_type>;
		static_assert(::std::same_as<declared, stored>, "sdag: 节点在接收者环境与整图环境下报告的值类型不一致");
		if constexpr (::std::same_as<stored, ::stdexec::completion_signatures<>>)
		{
			// 某个前驱永远不会带值结束（例如 just_stopped），这个节点也就永远不会被启动。
			::std::terminate();
		}
		else
		{
			deliver_entries<Dependency>(::std::make_index_sequence<delivery_table<Dependency>.size()>{}, ::std::forward<Receiver>(receiver));
		}
	}

	template<class Dependency, size_t... E, class Receiver>
	constexpr void deliver_entries(::std::index_sequence<E...>, Receiver&& receiver) noexcept
	{
		::stdexec::set_value(::std::forward<Receiver>(receiver), access<delivery_table<Dependency>[E]>()...);
	}
};

}
