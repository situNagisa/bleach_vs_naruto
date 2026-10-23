#pragma once

/// 节点怎么拿到前驱的值。
///
/// 思路：
///   用户写 make_node<b, after<a>>(then(f))。dag_t 构造图的时候把一个空的 input_sender<after<a>> 喂给 then，
///   得到 then(input_sender<after<a>>, f) 作为节点 b 的 sender——节点 sender 是普通的 stdexec 表达式，
///   then 的一切（包括 domain 对 then 的定制）都照常生效。
///
///   input_sender 本身不存值。它连接时拿到的接收者环境（node_env）里有一个转发查询 get_graph_state，
///   指向整图 op-state 里存值的那一层（graph_state）；启动时从那里把前驱的值一次性 set_value 给接收者。
///   前驱的值在前驱完成时存进 graph_state 的值槽，存多久由整图决定（整图结束前一直有效），
///   所以多个后继读同一个值时可以给 const&，不需要 split 那样的共享状态。
///
///   input_sender 的完成签名同样从环境里来：get_graph_state 的返回类型告诉它"我在哪张图里"，
///   再按前驱节点的签名拼出自己的签名。所以它（以及含有它的节点 sender、整个图）是依赖环境的 sender，
///   无环境时报 dependent_sender_error——和 read_env 一样。
///
/// 本文件：get_graph_state 查询、节点环境 node_env、input_sender、整图状态 graph_state（值槽 + 交付表）。

#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/completion_behavior.hpp>
#include <exec/completion_signatures.hpp> // exec::throw_compile_time_error

#include "../meta_program/type_list.h"
#include "../meta_program/utility.h"
#include "../meta_program/indexed_tuple.h"
#include "../fwd.h"
#include "../concurrency/stdexec_bridge.h"
#include "../graph/traits.h"
#include "../meta_program/rebind.h"
#include "../graph/analyze/graph_analysis.h"
#include "../graph/analyze/input_completions.h"
#include "../graph/analyze/consumers.h"

namespace sdag
{

namespace graph_detail
{

// ---------------------------------------------------------------- 查询：取回整图状态

/// 节点环境里的查询：input_sender 用它找到存放前驱值的整图状态。
/// 继承 forwarding_query_t，then / let_value / continues_on 等适配器包装环境时会把它原样转发下去。
struct get_graph_state_t : ::stdexec::forwarding_query_t
{
	template<class Env>
		requires requires(Env const& env) { env.query(::std::declval<get_graph_state_t>()); }
	[[nodiscard]] static constexpr auto operator()(Env const& env) noexcept
	{
		return env.query(get_graph_state_t{});
	}
};

inline constexpr get_graph_state_t get_graph_state{};

// ---------------------------------------------------------------- 节点环境

template<class Self, class Env>
struct graph_state;

/// 节点接收者的环境：整图状态指针 +（需要时）整图的停止令牌 + 外层环境的全部转发查询。
///
/// Stoppable 为假时没有停止令牌成员（0 字节），get_stop_token 也转发给外层：图里没有节点会失败，
/// 整图就不需要自己的停止源，节点直接看外层的令牌即可。
///
/// 图嵌套图时，内层节点的 node_env 包着外层节点的 node_env：内层的 get_graph_state 遮蔽外层的，
/// 其余查询（包括外层图的停止令牌）逐层转发。
template<class Self, class Env, bool Stoppable>
struct node_env
{
	graph_state<Self, Env>* _state;
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<Stoppable, ::stdexec::inplace_stop_token, meta::empty_for<node_env>> _stop_token;
	Env _outer;

	[[nodiscard]] constexpr graph_state<Self, Env>* query(get_graph_state_t) const noexcept
	{
		return _state;
	}

	[[nodiscard]] constexpr ::stdexec::inplace_stop_token query(::stdexec::get_stop_token_t) const noexcept
		requires Stoppable
	{
		return _stop_token;
	}

	template<class Query, class... Args>
		requires (!::std::same_as<Query, get_graph_state_t>)
			&& (!(Stoppable && ::std::same_as<Query, ::stdexec::get_stop_token_t>))
			&& (::stdexec::forwarding_query(Query{}))
			&& requires(Env const& outer, Args&&... args) { outer.query(Query{}, ::std::forward<Args>(args)...); }
	[[nodiscard]] constexpr decltype(auto) query(Query query, Args&&... args) const
		noexcept(noexcept(_outer.query(query, ::std::forward<Args>(args)...)))
	{
		return _outer.query(query, ::std::forward<Args>(args)...);
	}
};

/// 要不要停止源，取决于节点会不会失败；而节点的签名又取决于它看到的环境——鸡生蛋。
/// 解法：先在"不带停止令牌"的探测环境下算一遍签名。节点在探测环境下都不会 error / stopped / 存值抛异常，
/// 就不需要停止源（多出来的停止令牌也不会让一个本来不会失败的节点变得会失败）。
template<class Self, class Env>
using probe_env = node_env<Self, Env, false>;

template<class Self, class Env>
inline constexpr bool stoppable_v = analyze::analysis<Self, probe_env<Self, Env>>::can_fail;

/// 节点实际看到的环境。
template<class Self, class Env>
using node_env_t = node_env<Self, Env, stoppable_v<Self, Env>>;

/// 从环境里的整图状态指针类型反推图的类型。只看指针类型，不要求 graph_state 已经完整。
template<class Pointer>
struct graph_of;

template<class Self, class Env>
struct graph_of<graph_state<Self, Env>*>
{
	using type = Self;
};

template<class Env>
concept inside_graph = requires(Env const& env) { graph_detail::get_graph_state(env); };

template<class Env>
	requires inside_graph<Env>
using graph_of_env_t = typename graph_of<decltype(graph_detail::get_graph_state(::std::declval<Env const&>()))>::type;

// ---------------------------------------------------------------- 输入 sender 的操作

/// 输入 sender（以及降解探针里的占位 sender）在 start 里同步完成。
struct input_attrs
{
	template<class Tag>
	[[nodiscard]] static constexpr auto query(::exec::get_completion_behavior_t<Tag>) noexcept
	{
		return ::exec::completion_behavior::inline_completion;
	}
};

/// 输入 sender 在环境 Env 下的完成签名：从环境里认出所在的图，再按前驱节点的签名拼出来。
/// （单独写成函数：get_completion_signatures 的 Env 是包，不能直接展开进只接受一个 Env 的别名模板。）
template<class Dependency, class Env>
	requires inside_graph<Env>
[[nodiscard]] consteval auto input_completions_in()
{
	return analyze::input_completions_t<graph_of_env_t<Env>, Dependency, Env>{};
}

struct _THE_INPUT_SENDER_OF_A_DAG_NODE_WAS_CONNECTED_OUTSIDE_OF_A_DAG_;

template<class Dependency, class Receiver>
struct input_operation
{
	Receiver _receiver;

	constexpr void start() noexcept
	{
		auto* state = graph_detail::get_graph_state(::stdexec::get_env(_receiver));
		state->template deliver<Dependency>(::std::move(_receiver));
	}
};

}

// ---------------------------------------------------------------- 输入 sender

template<class Dependency>
struct input_sender
{
	using sender_concept = ::stdexec::sender_t;
	using dependency = Dependency;

	[[nodiscard]] static constexpr graph_detail::input_attrs get_env() noexcept
	{
		return {};
	}

	template<class Self, class... Env>
	[[nodiscard]] static consteval auto get_completion_signatures()
	{
		if constexpr (sizeof...(Env) == 0)
		{
			return concurrency::bridge::dependent_sender_error<Self>();
		}
		else if constexpr (!(graph_detail::inside_graph<Env> && ...))
		{
			return ::exec::throw_compile_time_error<graph_detail::_THE_INPUT_SENDER_OF_A_DAG_NODE_WAS_CONNECTED_OUTSIDE_OF_A_DAG_>();
		}
		else
		{
			return graph_detail::input_completions_in<Dependency, Env...>();
		}
	}

	template<::stdexec::receiver Receiver>
	[[nodiscard]] constexpr graph_detail::input_operation<Dependency, Receiver> connect(Receiver receiver) const
		noexcept(::std::is_nothrow_move_constructible_v<Receiver>)
	{
		return {::std::move(receiver)};
	}
};

namespace graph_detail
{

// ---------------------------------------------------------------- 整图状态：值槽

template<class Self, class Env, class Indices = ::std::make_index_sequence<count_of_v<Self>>>
struct slots;

/// 每个节点一个值槽：optional<tuple<衰变后的值...>>。不需要存值的节点（没人读、或唯一汇点直接转发）是 0 字节的 meta::empty。
template<class Self, class Env, size_t... I>
struct slots<Self, Env, ::std::index_sequence<I...>>
{
	using analysis = analyze::analysis<Self, Env>;

	template<size_t J>
	using slot_t = ::std::conditional_t<analysis::slot_needed[J],
		::std::optional<meta::rebind_t<::std::tuple, typename analysis::template values_t<J>>>,
		meta::empty>;

	using type = meta::indexed_tuple<slot_t<I>...>;
	static constexpr ::std::array<size_t, sizeof...(I)> value_counts{analysis::template values_t<I>::size...};
};

// ---------------------------------------------------------------- 交付表

/// 交付表的一项：从哪个节点的槽位取第几个值，按 const& 共享还是 move。
struct slot_access
{
	size_t node;
	size_t element;
	bool shared;
};

/// 把一组节点的值按顺序铺平成一张交付表。
///
/// 为什么要表：一个节点可能依赖多个前驱，每个前驱又有多个值，最后要拼成一次 set_value(值...)。
/// 直接写就是"带着已经取出的值一层层递归"。这里先在编译期把每个实参的来源算成一张平表，
/// 运行期只需要一次包展开 set_value(receiver, access<table[E]>()...)。
///
/// allow_shared 为假时一律 move（汇点的值交给整图接收者，之后没人再读）。
template<class Self, class Env, auto Nodes>
[[nodiscard]] consteval auto delivery_plan(bool allow_shared)
{
	constexpr auto counts = slots<Self, Env>::value_counts;
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
			plan[cursor++] = {node, element, allow_shared && consumer_counts_v<Self>[node] > 1};
		}
	}
	return plan;
}

// ---------------------------------------------------------------- 整图状态

/// 整图 op-state 里存值的那一层。融合 op-state（fused_core）从它派生；节点环境里的 get_graph_state 只暴露这一层，
/// 所以 input_sender 的签名计算只需要知道值槽的类型，不需要整个 op-state 完整。
template<class Self, class Env>
struct graph_state
{
	static_assert(analyze::analysis<Self, probe_env<Self, Env>>::valid, "sdag: 有节点在接收者环境下无法给出完成签名");
	static_assert(analyze::analysis<Self, node_env_t<Self, Env>>::valid, "sdag: 有节点在整图环境下无法给出完成签名");

	static constexpr auto shape = shape_of_v<Self>;
	static constexpr bool stoppable = stoppable_v<Self, Env>;
	using env_type = node_env_t<Self, Env>;
	using analysis_type = analyze::analysis<Self, env_type>;

	SDAG_NO_UNIQUE_ADDRESS typename slots<Self, env_type>::type _slots{};

	/// 按交付表的一项取值：共享的给 const&，独占的 move 出去。
	template<slot_access Access>
	[[nodiscard]] constexpr decltype(auto) access() noexcept
	{
		auto& value = ::std::get<Access.element>(*meta::get<Access.node>(_slots));
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
	static constexpr auto delivery_table = graph_detail::delivery_plan<Self, env_type, dependency_indices_v<Self, Dependency>>(true);

	/// input_operation 启动时调用：把 Dependency 对应前驱的值交给接收者。
	template<class Dependency, class Receiver>
	constexpr void deliver(Receiver&& receiver) noexcept
	{
		// 接收者那一侧是在它自己的环境（node_env 外面可能又包了几层适配器）下算出输入签名的，
		// 这里是在整图环境下存的值。两边必须一致，否则说明某个节点 sender 在不同环境下报告了不同的值类型。
		using declared = analyze::input_completions_t<Self, Dependency, ::stdexec::env_of_t<Receiver>>;
		using stored = analyze::input_completions_t<Self, Dependency, env_type>;
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

}
