#pragma once

/// 图 sender 的行为：节点环境、整图状态（值槽与交付）、输入 sender、完成签名、属性、降解判据、dag_t。
///
/// 这里只用编译期计算层（compute::）与 stdexec 的公开接口；stdexec 内部名字只经 stdexec_bridge.h。
/// 真正的 operation state 在 fused.h，向 when_all / split 组合的降解在 composed.h。

#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>
#include <exec/completion_behavior.hpp>
#include <exec/completion_signatures.hpp>
#include <exec/split.hpp>

#include "meta.h"
#include "fwd.h"
#include "node.h"
#include "shape.h"
#include "stdexec_bridge.h"
#include "graph_sender.h"
#include "compute/primitives.h"
#include "compute/analysis.h"

namespace sdag
{

namespace graph_detail
{

// ---------------------------------------------------------------- 查询：取回整图状态

/// 节点环境里的转发查询：input_sender 用它找到存放前驱值的整图状态。
/// 继承 forwarding_query_t 让 then / let_value / continues_on 等适配器的环境包装都放行。
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

/// 节点接收者的环境：整图状态指针 + （需要时）整图停止令牌 + 外层环境的转发查询。
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

template<class Self, class Env>
using probe_env = node_env<Self, Env, false>;

/// 先在"只转发外层停止令牌"的环境下探测：没有任何节点会失败 / 停止 / 抛异常，就不需要停止源。
template<class Self, class Env>
inline constexpr bool stoppable_v = compute::analysis<Self, probe_env<Self, Env>>::can_fail;

template<class Self, class Env>
using node_env_t = node_env<Self, Env, stoppable_v<Self, Env>>;

/// 从环境里的整图状态指针类型反推图的类型；不要求 graph_state 完整。
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

// ---------------------------------------------------------------- 完成签名

/// 有接收者环境时的完成签名：先按探测环境、再按最终节点环境检查每个节点；不合法的结果原样透传给 stdexec。
template<class Self, class Env>
[[nodiscard]] consteval auto completions_with_env()
{
	using probe = compute::analysis<Self, probe_env<Self, Env>>;
	if constexpr (!probe::valid)
	{
		return typename probe::first_invalid_t{};
	}
	else
	{
		using final = compute::analysis<Self, node_env_t<Self, Env>>;
		if constexpr (!final::valid)
		{
			return typename final::first_invalid_t{};
		}
		else
		{
			return typename final::completions{};
		}
	}
}

template<class Self, class... Env>
[[nodiscard]] consteval auto completions_of()
{
	if constexpr (sizeof...(Env) == 0)
	{
		// 输入 sender 依赖环境，所以整图在无环境下必然是 dependent sender。
		return bridge::dependent_sender_error<::std::remove_cvref_t<Self>>();
	}
	else
	{
		return graph_detail::completions_with_env<Self, Env...>();
	}
}

// ---------------------------------------------------------------- 输入 sender

/// 输入 sender（与探针占位 sender）在 start 里内联完成。
struct input_attrs
{
	template<class Tag>
	[[nodiscard]] static constexpr auto query(::exec::get_completion_behavior_t<Tag>) noexcept
	{
		return ::exec::completion_behavior::inline_completion;
	}
};

template<class Dependency, class Env>
	requires inside_graph<Env>
[[nodiscard]] consteval auto input_completions_from_env()
{
	return compute::input_completions_t<graph_of_env_t<Env>, Dependency, Env>{};
}

struct _THE_INPUT_SENDER_OF_A_DAG_NODE_WAS_CONNECTED_OUTSIDE_OF_A_DAG_;

template<class Dependency, class Receiver>
struct input_operation
{
	Receiver _receiver;

	void start() noexcept
	{
		auto* state = graph_detail::get_graph_state(::stdexec::get_env(_receiver));
		state->template deliver<Dependency>(::std::move(_receiver));
	}
};

}

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
			return bridge::dependent_sender_error<Self>();
		}
		else if constexpr (!(graph_detail::inside_graph<Env> && ...))
		{
			return ::exec::throw_compile_time_error<graph_detail::_THE_INPUT_SENDER_OF_A_DAG_NODE_WAS_CONNECTED_OUTSIDE_OF_A_DAG_>();
		}
		else
		{
			return graph_detail::input_completions_from_env<Dependency, Env...>();
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

// ---------------------------------------------------------------- 整图状态：值槽与交付

template<class Self, class Env, class Indices = ::std::make_index_sequence<count_of_v<Self>>>
struct slots;

/// 每个节点一个值槽：optional<tuple<值...>>；不需要存值的节点是 0 字节的 meta::empty。
template<class Self, class Env, size_t... I>
struct slots<Self, Env, ::std::index_sequence<I...>>
{
	using analysis = compute::analysis<Self, Env>;

	template<size_t J>
	using slot_t = ::std::conditional_t<analysis::slot_needed[J],
		::std::optional<compute::rebind_t<::std::tuple, typename analysis::template values_t<J>>>,
		meta::empty>;

	using type = meta::indexed_tuple<slot_t<I>...>;
	static constexpr ::std::array<size_t, sizeof...(I)> value_counts{analysis::template values_t<I>::size...};
};

/// 交付表的一项：从哪个节点的槽位取第几个值，按 const& 共享还是 move。
struct slot_access
{
	size_t node;
	size_t element;
	bool shared;
};

/// 把一组节点的值按顺序铺平成交付表；allow_shared 为假时一律 move（汇点交给整图接收者）。
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

/// 节点值的存放处；fused_core 从它派生，节点环境里只暴露这一层。
template<class Self, class Env>
struct graph_state
{
	static_assert(compute::analysis<Self, probe_env<Self, Env>>::valid, "sdag: 有节点在接收者环境下无法给出完成签名");
	static_assert(compute::analysis<Self, node_env_t<Self, Env>>::valid, "sdag: 有节点在整图环境下无法给出完成签名");

	static constexpr auto shape = shape_of_v<Self>;
	static constexpr size_t count = shape.count;
	static constexpr bool stoppable = stoppable_v<Self, Env>;
	using env_type = node_env_t<Self, Env>;
	using analysis_type = compute::analysis<Self, env_type>;
	using completions = typename analysis_type::completions;

	template<size_t J>
	using values_t = typename analysis_type::template values_t<J>;

	template<size_t J>
	static constexpr bool slot_needed = analysis_type::slot_needed[J];

	SDAG_NO_UNIQUE_ADDRESS typename slots<Self, env_type>::type _slots{};

	/// 按交付表的一项取值：共享的给 const&，独占的 move 出去。
	template<slot_access Access>
	[[nodiscard]] decltype(auto) access() noexcept
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
	void deliver(Receiver&& receiver) noexcept
	{
		// 接收者那条链路是在它自己的环境下算出输入签名的；必须与这里存的槽位一致，
		// 否则说明某个节点 sender 在不同环境下报告了不同的值类型。
		using declared = compute::input_completions_t<Self, Dependency, ::stdexec::env_of_t<Receiver>>;
		using stored = compute::input_completions_t<Self, Dependency, env_type>;
		static_assert(::std::same_as<declared, stored>, "sdag: 节点在接收者环境与整图环境下报告的值类型不一致");
		if constexpr (::std::same_as<stored, ::stdexec::completion_signatures<>>)
		{
			// 某个前驱永远不会带值结束，这个节点也就永远不会被启动。
			::std::terminate();
		}
		else
		{
			deliver_entries<Dependency>(::std::make_index_sequence<delivery_table<Dependency>.size()>{}, ::std::forward<Receiver>(receiver));
		}
	}

	template<class Dependency, size_t... E, class Receiver>
	void deliver_entries(::std::index_sequence<E...>, Receiver&& receiver) noexcept
	{
		::stdexec::set_value(::std::forward<Receiver>(receiver), access<delivery_table<Dependency>[E]>()...);
	}
};

}

// ---------------------------------------------------------------- 降解入口（定义在 composed.h）

/// lower(graph, fanin, fanout)：把图 sender 降解为 fanin / fanout / let_value 组合的普通 sender 树。
struct lower_t
{
	template<class Graph, class FanIn, class FanOut>
	[[nodiscard]] auto operator()(Graph&& graph, FanIn const& fanin, FanOut const& fanout) const;
};

inline constexpr lower_t lower{};

namespace graph_detail
{

// ---------------------------------------------------------------- 节点

/// 节点的 sender：载荷是 sender 就原样取出，否则把输入 sender 喂给它。
template<class Node>
[[nodiscard]] constexpr auto make_child(Node&& node)
{
	using dependency = node_dependencies_t<Node>;
	using payload_type = decltype(::sdag::payload(::std::forward<Node>(node)));
	static_assert(node_payload<payload_type, input_sender<dependency>>,
		"sdag: 节点载荷既不是 sender，也不是能接受输入 sender 的适配器（例如 ::stdexec::then(f)）");
	if constexpr (sender_payload<payload_type>)
	{
		static_assert(compute::input_count_v<::std::decay_t<payload_type>> == 0, "sdag: 只有控制依赖的节点 sender 里不能出现输入 sender");
		return ::std::decay_t<payload_type>(::sdag::payload(::std::forward<Node>(node)));
	}
	else
	{
		using child = decltype(::sdag::payload(::std::forward<Node>(node))(input_sender<dependency>{}));
		static_assert(compute::input_count_v<child> == 1,
			"sdag: 节点适配器必须把输入 sender 恰好用一次，而且中间只能隔着 stdexec 的算法层："
			"它的值只会被交付一次，repeat / retry 这类会重启子 sender 的适配器不能套在输入上；"
			"不透明的自定义 sender 包着输入时这里会数成 0 次");
		return ::sdag::payload(::std::forward<Node>(node))(input_sender<dependency>{});
	}
}

// ---------------------------------------------------------------- 降解判据

/// 探针用的占位 sender：完成签名与节点真实的输入 sender 在整图环境下一致，属性也一致；只用来算类型，不会被连接。
template<class Completions>
struct placeholder_sender
{
	using sender_concept = ::stdexec::sender_t;

	template<class Self, class... Env>
	[[nodiscard]] static consteval auto get_completion_signatures() noexcept
	{
		return Completions{};
	}

	[[nodiscard]] static constexpr input_attrs get_env() noexcept
	{
		return {};
	}
};

template<class Self, class Env, size_t Sink>
using probe_sink_t = compute::substituted_t<child_t<Self, Sink>,
	placeholder_sender<compute::input_completions_t<Self, dependency_t<Self, Sink>, node_env_t<Self, Env>>>>;

template<class Self, class Env, class Sinks = compute::sequence_t<sinks_v<shape_of_v<Self>>>>
struct when_all_probe;

/// 探针：when_all(汇点 sender（输入叶子换成占位 sender）...)。保留汇点的属性（完成调度器等），
/// 签名与真实降解后的汇点一致，又不需要整图环境就能被 domain 的 transform_sender 检查。
/// when_all 只有一个子 sender 时原样返回它，所以只有一个汇点时把它重复一次。
template<class Self, class Env, size_t... S>
struct when_all_probe<Self, Env, ::std::index_sequence<S...>>
{
	static consteval auto compute() noexcept
	{
		if constexpr (sizeof...(S) == 1)
		{
			return ::std::type_identity<decltype(::stdexec::when_all(::std::declval<probe_sink_t<Self, Env, S>>()...,
				::std::declval<probe_sink_t<Self, Env, S>>()...))>{};
		}
		else
		{
			return ::std::type_identity<decltype(::stdexec::when_all(::std::declval<probe_sink_t<Self, Env, S>>()...))>{};
		}
	}

	using type = typename decltype(compute())::type;
};

/// transform_sender 分两个阶段：start_t 用环境的起始 domain，set_value_t 用 sender 自己报告的完成域
/// （get_completion_domain<>：先问 void 版本，再回落到 set_value_t，与 stdexec 的规则一致）。
template<class OpTag, class Self, class Env>
[[nodiscard]] consteval auto phase_domain() noexcept
{
	if constexpr (::std::same_as<OpTag, ::stdexec::start_t>)
	{
		return ::std::type_identity<decltype(::stdexec::get_domain(::std::declval<Env const&>()))>{};
	}
	else if constexpr (requires { ::stdexec::get_completion_domain<>(::stdexec::get_env(::std::declval<Self>()), ::std::declval<Env const&>()); })
	{
		return ::std::type_identity<decltype(::stdexec::get_completion_domain<>(
			::stdexec::get_env(::std::declval<Self>()), ::std::declval<Env const&>()))>{};
	}
	else
	{
		return ::std::type_identity<::stdexec::default_domain>{};
	}
}

/// domain 的 transform_sender 会不会改写 Sender：有这个成员，并且结果与 default_domain 的不同。
template<class Domain, class OpTag, class Sender, class Env>
concept customizes = requires { ::std::declval<Domain>().transform_sender(OpTag{}, ::std::declval<Sender>(), ::std::declval<Env const&>()); }
	&& !::std::same_as<
		decltype(::std::declval<Domain>().transform_sender(OpTag{}, ::std::declval<Sender>(), ::std::declval<Env const&>())),
		decltype(::stdexec::default_domain{}.transform_sender(OpTag{}, ::std::declval<Sender>(), ::std::declval<Env const&>()))>;

/// 当前阶段的 domain 会不会定制覆盖在汇点上的 when_all：会，整图就降解成标准算法交给它；
/// 不会（默认 domain、只定制 bulk 的线程池 domain 等）就保持融合。节点签名不合法时返回 false，把诊断留给 connect。
template<class OpTag, class Self, class Env>
[[nodiscard]] consteval bool lowering_wanted_in() noexcept
{
	if constexpr (!compute::analysis<Self, probe_env<Self, Env>>::valid)
	{
		return false;
	}
	else if constexpr (!compute::analysis<Self, node_env_t<Self, Env>>::valid)
	{
		return false;
	}
	else
	{
		using domain = typename decltype(graph_detail::phase_domain<OpTag, Self, Env>())::type;
		if constexpr (::std::same_as<domain, ::stdexec::default_domain>)
		{
			return false;
		}
		else
		{
			return customizes<domain, OpTag, typename when_all_probe<Self, Env>::type, Env>;
		}
	}
}

template<class OpTag, class Self, class Env>
inline constexpr bool lowering_wanted = graph_detail::lowering_wanted_in<OpTag, Self, Env>();

// ---------------------------------------------------------------- 属性（与 when_all 的属性同一套语义）

/// 完成域：值完成发生在最后一个汇点完成的地方——取汇点在整图节点环境下的公共完成域；
/// 错误 / 停止可能来自任一节点——取全部节点的公共域。完成行为取全部节点的合成。
template<class Data, class... Children>
struct attrs
{
	using self_type = graph_sender<Data, Children...>&&;

	template<class Env>
	[[nodiscard]] static consteval bool evaluable_in() noexcept
	{
		if constexpr (!compute::analysis<self_type, probe_env<self_type, Env>>::valid)
		{
			return false;
		}
		else
		{
			return compute::analysis<self_type, node_env_t<self_type, Env>>::valid;
		}
	}

	template<class Env, class Indices = compute::sequence_t<sinks_v<Data::shape>>>
	struct sink_children;

	template<class Env, size_t... S>
	struct sink_children<Env, ::std::index_sequence<S...>>
	{
		using type = meta::type_list<Children...[S]...>;
	};

	template<class Env>
	using value_domain_t = compute::common_completion_domain_t<node_env_t<self_type, Env>, typename sink_children<Env>::type>;

	template<class Env>
	using any_node_domain_t = compute::common_completion_domain_t<node_env_t<self_type, Env>, meta::type_list<Children...>>;

	template<class Env>
		requires (evaluable_in<Env>()) && (!::std::is_void_v<value_domain_t<Env>>)
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_value_t>, Env const&) const noexcept
		-> value_domain_t<Env>
	{
		return {};
	}

	template<class Env>
	using failure_domain_t = compute::common_domain_t<meta::type_list<value_domain_t<Env>, any_node_domain_t<Env>>>;

	template<class Env>
		requires (evaluable_in<Env>()) && (!::std::is_void_v<value_domain_t<Env>>) && (!::std::is_void_v<any_node_domain_t<Env>>)
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_error_t>, Env const&) const noexcept
		-> failure_domain_t<Env>
	{
		return {};
	}

	template<class Env>
		requires (evaluable_in<Env>()) && (!::std::is_void_v<value_domain_t<Env>>) && (!::std::is_void_v<any_node_domain_t<Env>>)
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_stopped_t>, Env const&) const noexcept
		-> failure_domain_t<Env>
	{
		return {};
	}

	/// 子 sender 的完成行为：直接调公开的查询对象，结果是类型化的常量（exec::completion_behavior::*_t）。
	template<class Tag, class Child, class... Env>
	using child_behavior_t = decltype(::exec::get_completion_behavior_t<Tag>{}(
		::stdexec::get_env(::std::declval<Child const&>()), ::std::declval<Env const&>()...));

	template<class Tag, class... Env>
	[[nodiscard]] constexpr auto query(::exec::get_completion_behavior_t<Tag>, Env const&...) const noexcept
	{
		return ::exec::completion_behavior::common(child_behavior_t<Tag, Children, Env...>{}...);
	}
};

template<class Data>
struct attrs<Data>
{
	template<class Tag>
	[[nodiscard]] constexpr auto query(::exec::get_completion_behavior_t<Tag>) const noexcept
	{
		return ::exec::completion_behavior::inline_completion;
	}
};

}

// ---------------------------------------------------------------- 算法标签

/// 图算法的标签。dag_t{}(nodes...) 得到图 sender；dag() 是它的策略化包装。
struct dag_t
{
	template<graph_node... Nodes>
	[[nodiscard]] constexpr auto operator()(Nodes&&... nodes) const
	{
		using data = graph_detail::dag_data<meta::type_list<node_key_t<Nodes>...>, meta::type_list<node_dependencies_t<Nodes>...>>;
		using sender = graph_sender<data, decltype(graph_detail::make_child(::std::forward<Nodes>(nodes)))...>;
		// stdexec 靠结构化绑定识别标签；编译器不支持 __builtin_structured_binding_size 时这里会失效，降解也就静默关闭，所以显式检查。
		static_assert(::std::same_as<::stdexec::tag_of_t<sender>, dag_t>, "sdag: stdexec 认不出图 sender 的标签（编译器缺少结构化绑定内省）");
		using children = meta::indexed_tuple<decltype(graph_detail::make_child(::std::forward<Nodes>(nodes)))...>;
		return sender{data{}, children(meta::from_factory, [&] { return graph_detail::make_child(::std::forward<Nodes>(nodes)); }...)};
	}

	/// 所在 domain 会定制 when_all 时，把整图降解为 when_all / split / let_value 组合，让它吃到定制；
	/// 否则（默认 domain、只定制 bulk 的线程池 domain 等）不声明此重载，走 fused.h 的融合 op-state。
	template<class OpTag, class Sender, class Env>
		requires (graph_detail::lowering_wanted_in<OpTag, Sender&&, Env>())
	[[nodiscard]] static auto transform_sender(OpTag, Sender&& sender, Env const&)
	{
		return ::sdag::lower(::std::forward<Sender>(sender), ::stdexec::when_all, ::exec::split);
	}
};

/// 图 sender 的形状（邻接矩阵、入出度、拓扑序）。
template<class Graph>
constexpr auto shape_v = graph_detail::shape_of_v<Graph&&>;

}
