#pragma once

/// 值槽与交付：前驱的值存哪里、怎么按序交给读者；实现取值源的「交付接口」。
/// 依赖：图的描述、值的读者、编译期序列、存储计划、不可移动元组、取值源协议、节点所见环境。
///
///   graph_state<Self, Env>   整图 op-state 里存值的那一层，也是这张图的取值源：
///                            签名接口由 protocol::value_source_traits 的特化转到 pred_signatures<Self>（graph/pred_sigs.h），交付接口是这里的 deliver。
///                            融合 op-state 从它派生；节点环境里只暴露这一层的指针。
///   slot_access              交付表的一项：从哪个节点的槽位取第几个值，按 const& 共享还是 move
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
#include "../protocol/value_source.h"
#include "description.h"
#include "readers.h"
#include "analysis.h"
#include "storage_plan.h"
#include "pred_sigs.h"
#include "visibility.h"

namespace sdag::graph
{

template<class Self, class Env>
struct graph_state;

/// 这张图在环境 Env 下的节点环境：取值源就是整图状态本身。
template<class Self, class Env>
using graph_node_env_t = node_env_t<graph_state, Self, Env>;

}

/// 整图状态的签名接口在类外提供：节点签名算出来之前 graph_state 还是不完整类型（见 protocol/value_source.h）。
template<class Self, class Env>
struct sdag::protocol::value_source_traits<sdag::graph::graph_state<Self, Env>> : sdag::graph::pred_signatures<Self>
{
};

namespace sdag::graph
{


namespace slots_detail
{

template<class Self, class Env, class Indices = ::std::make_index_sequence<count_of_v<Self>>>
struct slots;

/// 每个节点一个值槽：optional<tuple<衰变后的值...>>。不需要存值的节点（没人读、或唯一汇点直接转发）是 0 字节的 empty。
template<class Self, class Env, size_t... I>
struct slots<Self, Env, ::std::index_sequence<I...>>
{
	using analysis = node_analysis<Self, Env>;
	using plan = storage_plan<Self, Env>;

	template<size_t J>
	using slot_t = ::std::conditional_t<plan::slot_needed[J],
		::std::optional<general::rebind_t<::std::tuple, typename analysis::template values_t<J>>>,
		general::empty>;

	using type = general::indexed_tuple<slot_t<I>...>;
	static constexpr ::std::array<size_t, sizeof...(I)> value_counts{analysis::template values_t<I>::size...};
};

}

struct slot_access
{
	size_t node;
	size_t element;
	bool shared;
};

/// allow_shared 为假时一律 move（汇点的值交给整图接收者，之后没人再读）。
template<class Self, class Env, auto Nodes>
[[nodiscard]] consteval auto delivery_plan(bool allow_shared)
{
	constexpr auto counts = slots_detail::slots<Self, Env>::value_counts;
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
	for (size_t node : Nodes)
	{
		for (size_t element = 0; element < counts[node]; ++element)
		{
			plan[cursor++] = {node, element, allow_shared && reader_counts_v<Self>[node] > 1};
		}
	}
	return plan;
}

template<class Self, class Env>
struct graph_state
{
	using env_type = graph_node_env_t<Self, Env>;
	using analysis_type = node_analysis<Self, env_type>;
	using plan_type = storage_plan<Self, env_type>;

	static_assert(node_analysis<Self, probe_env_t<graph::graph_state, Self, Env>>::valid, "sdag: 有节点在接收者环境下无法给出完成签名");
	static_assert(analysis_type::valid, "sdag: 有节点在整图环境下无法给出完成签名");

	static constexpr auto shape = shape_of_v<Self>;
	static constexpr bool stoppable = stoppable_v<graph::graph_state, Self, Env>;

	SDAG_NO_UNIQUE_ADDRESS typename slots_detail::slots<Self, env_type>::type _slots{};

	/// 按交付表的一项取值：共享的给 const&，独占的 move 出去。
	template<slot_access Access>
	[[nodiscard]] constexpr decltype(auto) access() noexcept
	{
		auto& value = ::std::get<Access.element>(*general::get<Access.node>(_slots));
		if constexpr (Access.shared)
		{
			return ::std::as_const(value);
		}
		else
		{
			return ::std::move(value);
		}
	}

	/// Dependency（after<...>）对应前驱的值，按声明顺序铺平。
	template<class Dependency>
	static constexpr auto delivery_table = graph::delivery_plan<Self, env_type, dependency_indices_v<Self, Dependency>>(true);

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
