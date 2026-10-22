#pragma once

/// 图的 sender 形态：一个以 dag_t 为标签的 ::stdexec::__sexpr，data 是编译期的键 / 依赖表，
/// children 就是各节点的 sender（用户的 then / continues_on / let_value 表达式原样保留）。
///
/// 这样整张图就是 stdexec 生态里的普通算法节点：tag_of_t / sender_for / 子节点内省 /
/// transform_sender 定制都成立；节点 sender 连接时照常被所在 domain 变换。
///
/// 本文件只有类型层面的分析与节点环境；真正的 operation state 在 fused.h，
/// 向 when_all / split 组合的降解在 composed.h。

#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/completion_signatures.hpp>

#include "meta.h"
#include "signatures.h"
#include "node.h"
#include "shape.h"

namespace sdag
{

struct dag_t;

/// 节点 I 的输入 sender（输入 keys 就是它 after<...> 里的键）：启动时把前驱的值一次性 set_value 给接收者。
/// 本身没有状态，完成签名由接收者环境里的整图状态类型决定——这是一个"依赖环境"的 sender，像 read_env。
template<class Keys>
struct input_sender;

namespace graph_detail
{

using ::std::size_t;

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

// ---------------------------------------------------------------- 静态数据与特征

/// sexpr 的 data：只有类型，运行期是空对象。
template<class Keys, class Dependencies>
struct dag_data
{
	using keys = Keys;
	using dependencies = Dependencies;
	static constexpr auto shape = checked_shape_of_lists<Keys, Dependencies>();
	static constexpr size_t count = Keys::size;

	template<size_t I>
	using dependencies_t = keys_to_indices_t<Keys, typename meta::at_t<I, Dependencies>::keys>;
};

template<class Sender>
struct sender_traits;

template<class Data, class... Children>
struct sender_traits<::stdexec::__sexpr<dag_t, Data, Children...>>
{
	using data = Data;
	using children = meta::type_list<Children...>;
	static constexpr auto shape = Data::shape;
	static constexpr size_t count = sizeof...(Children);

	template<size_t I>
	using child_t = meta::at_t<I, children>;

	template<size_t I>
	using dependencies_t = typename Data::template dependencies_t<I>;
};

/// Self 永远是图 sender 的引用类型（X&& 或 X const&），调用方先规范化再传入。
template<class Self>
using traits_of = sender_traits<::std::remove_cvref_t<Self>>;

template<class Self, size_t I>
using node_sender_t = ::stdexec::__copy_cvref_t<Self, typename traits_of<Self>::template child_t<I>>;

// ---------------------------------------------------------------- 节点 sender 里的输入叶子

template<class T>
inline constexpr size_t input_count_v = 0;

template<class Keys>
inline constexpr size_t input_count_v<input_sender<Keys>> = 1;

template<class T>
	requires requires { typename T::__children_t; }
inline constexpr size_t input_count_v<T> = []<class... Children>(::stdexec::__mlist<Children...>*)
{
	return (input_count_v<::std::remove_cvref_t<Children>> + ... + size_t{0});
}(static_cast<typename T::__children_t*>(nullptr));

template<class T>
inline constexpr bool contains_input_v = input_count_v<T> != 0;

/// 节点 I 是否消费前驱的值：含输入叶子的节点消费，纯 sender 载荷的节点只有控制依赖。
template<class Self, size_t I>
inline constexpr bool consumes_input = contains_input_v<typename traits_of<Self>::template child_t<I>>;

template<class Self, size_t D, class Indices>
struct consumer_count;

template<class Self, size_t D, size_t... I>
struct consumer_count<Self, D, ::std::index_sequence<I...>>
{
	static constexpr size_t value = (((traits_of<Self>::shape.edge[D][I] && consumes_input<Self, I>) ? size_t{1} : size_t{0}) + ... + size_t{0});
};

/// 节点 D 的值要不要共享：读它的值的后继（只有控制依赖的后继不算）超过一个才共享，否则 move 给唯一的消费者。
template<class Self, size_t D>
inline constexpr bool shared_v = consumer_count<Self, D, ::std::make_index_sequence<traits_of<Self>::count>>::value > 1;

template<auto Shape>
constexpr auto sink_indices = meta::filtered_indices<Shape.count, [](size_t index) { return Shape.is_sink(index); }>;

template<auto Shape>
constexpr auto root_indices = meta::filtered_indices<Shape.count, [](size_t index) { return Shape.is_root(index); }>;

// ---------------------------------------------------------------- 把输入叶子换成别的 sender（只算类型）

template<class T>
inline constexpr bool is_input_v = false;

template<class Keys>
inline constexpr bool is_input_v<input_sender<Keys>> = true;

template<class CvSender, class Input>
struct substituted;

template<class CvSender, class Input>
using substituted_t = typename substituted<CvSender, Input>::type;

template<class CvSender, class Input, class Children>
struct rebuilt;

/// sexpr 原地重建：标签、data 不变，子 sender 递归替换。重建出的是裸 __sexpr<Tag, Data, Child...>，
/// 与算法自己的派生 sender 类型共用同一个 __sexpr_impl<Tag>。
template<class CvSender, class Input, class... Children>
struct rebuilt<CvSender, Input, ::stdexec::__mlist<Children...>>
{
	using sender = ::std::remove_cvref_t<CvSender>;
	using type = ::stdexec::__sexpr<typename sender::__tag_t, ::std::decay_t<typename sender::__data_t>,
		substituted_t<::stdexec::__copy_cvref_t<CvSender, Children>, Input>...>;
};

template<class CvSender, class Input>
struct substituted
{
	using sender = ::std::remove_cvref_t<CvSender>;

	static consteval auto compute() noexcept
	{
		if constexpr (is_input_v<sender>)
		{
			return meta::tag<::std::decay_t<Input>>{};
		}
		else if constexpr (!contains_input_v<sender>)
		{
			return meta::tag<sender>{};
		}
		else
		{
			static_assert(requires { typename sender::__children_t; },
				"sdag: 节点 sender 里有一层不是 stdexec 的 sexpr，无法把输入叶子替换进去；降解只支持由 stdexec 算法组成的节点");
			return meta::tag<typename rebuilt<CvSender, Input, typename sender::__children_t>::type>{};
		}
	}

	using type = typename decltype(compute())::type;
};

/// 输入 sender（与探针占位 sender）在 start 里内联完成。
struct input_attrs
{
	template<class Tag>
	[[nodiscard]] static constexpr auto query(::stdexec::__get_completion_behavior_t<Tag>) noexcept
	{
		return ::stdexec::__completion_behavior::__inline_completion;
	}
};

// ---------------------------------------------------------------- 完成签名分析

template<class Self, size_t I, class... Env>
using node_completions_t = decltype(::stdexec::get_completion_signatures<node_sender_t<Self, I>, Env...>());

template<class Completions>
struct node_summary
{
	using summary = sig::summary<Completions>;
	static_assert(summary::value_lists::size <= 1,
		"sdag: 每个节点至多只能有一种 set_value 完成；有多种时请先 ::stdexec::into_variant");

	/// 没有值完成的节点（例如 just_stopped()）永远不会带着值结束，它的后继也永远不会以值启动。
	static constexpr bool has_values = summary::value_lists::size == 1;
	using raw_values = meta::at_t<0, meta::concat_t<typename summary::value_lists, meta::type_list<meta::type_list<>>>>;
	using raw_errors = typename summary::error_types;
	using values = meta::transform_t<sig::decay_t, raw_values>;
	using errors = meta::transform_t<sig::decay_t, raw_errors>;
	static constexpr bool sends_stopped = summary::sends_stopped;
	/// 存进 op-state 的拷贝 / 移动是否不抛；抛的话整图多一个 exception_ptr 错误完成。
	static constexpr bool nothrow_values = sig::all_nothrow_decay_copyable<raw_values>::value;
	static constexpr bool nothrow_errors = sig::all_nothrow_decay_copyable<raw_errors>::value;
};

/// 唯一汇点时整图的值签名就是它的原始签名；否则是各汇点衰变后的值拼接。延迟到确定 Direct 之后再取下标。
template<class Self, bool Direct, size_t Sink, class Fallback, class... Env>
struct graph_value_types
{
	using type = Fallback;
};

template<class Self, size_t Sink, class Fallback, class... Env>
struct graph_value_types<Self, true, Sink, Fallback, Env...>
{
	using type = typename node_summary<node_completions_t<Self, Sink, Env...>>::raw_values;
};

template<class Self, class Indices, class... Env>
struct analysis_impl;

template<class Self, size_t... I, class... Env>
struct analysis_impl<Self, ::std::index_sequence<I...>, Env...>
{
	static constexpr auto shape = traits_of<Self>::shape;

	template<size_t J>
	using node = node_summary<node_completions_t<Self, J, Env...>>;

	template<size_t J>
	using values_t = typename node<J>::values;

	/// 只有一个汇点时它的值不存槽位：完成时直接转发给整图的接收者（与 then 一样不物化）。
	static constexpr bool direct_sink = shape.sink_count() == 1;
	static constexpr size_t sink_index = direct_sink ? sink_indices<shape>[0] : size_t{0};

	/// 值没人读（既不是要存值的汇点、后继又都只是控制依赖）或者根本没有值时，不占存储。
	template<size_t J>
	static constexpr bool slot_needed = values_t<J>::size != 0
		&& ((shape.is_sink(J) && !direct_sink) || ((shape.edge[J][I] && consumes_input<Self, I>) || ...));

	static constexpr bool sends_stopped = (node<I>::sends_stopped || ...);
	static constexpr bool has_errors = ((node<I>::errors::size != 0) || ...);
	/// 存值 / 存错误会不会抛：不存槽位的值不算。
	static constexpr bool nothrow_store = (node<I>::nothrow_errors && ...) && ((!slot_needed<I> || node<I>::nothrow_values) && ...);
	static constexpr bool can_fail = has_errors || sends_stopped || !nothrow_store;
	/// 任一节点永远不会带值结束，整图就永远不会以 set_value 完成（每个节点都是某个汇点的祖先）。
	static constexpr bool has_values = (node<I>::has_values && ...);

	using error_types = meta::unique_t<meta::concat_t<
		typename node<I>::errors...,
		::std::conditional_t<nothrow_store, meta::type_list<>, meta::type_list<::std::exception_ptr>>>>;
	using value_types = typename graph_value_types<Self, direct_sink, sink_index,
		meta::concat_t<::std::conditional_t<shape.is_sink(I), values_t<I>, meta::type_list<>>...>, Env...>::type;
	using completions = sig::make_completions_t<has_values, value_types, error_types, sends_stopped>;
};

template<class Self, class... Env>
using analysis = analysis_impl<Self, ::std::make_index_sequence<traits_of<Self>::count>, Env...>;

template<class Self, class Indices, class... Env>
struct validity;

template<class Self, size_t... I, class... Env>
struct validity<Self, ::std::index_sequence<I...>, Env...>
{
	static constexpr bool value = (sig::is_completion_signatures<node_completions_t<Self, I, Env...>> && ...);

	/// 第一个不合法的结果原样返回（依赖环境的 sender 在无环境下的错误对象等）。
	template<size_t Cursor = 0>
	static consteval auto first_invalid()
	{
		constexpr ::std::array<size_t, sizeof...(I)> indices{I...};
		if constexpr (Cursor == sizeof...(I))
		{
			::std::unreachable();
		}
		else if constexpr (!sig::is_completion_signatures<node_completions_t<Self, indices[Cursor], Env...>>)
		{
			return node_completions_t<Self, indices[Cursor], Env...>{};
		}
		else
		{
			return first_invalid<Cursor + 1>();
		}
	}
};

template<class Self, class... Env>
using all_nodes_validity = validity<Self, ::std::make_index_sequence<traits_of<Self>::count>, Env...>;

// ---------------------------------------------------------------- 按拓扑序预热

/// 节点 sender 的完成签名要先算前驱的：then(f) 问子 sender 的签名时，节点 N 的计算会嵌套进
/// 节点 N-1 的……每层几十个实例化帧，长链会撞上编译器的模板深度上限。这里按拓扑序逐个
/// 实例化（基类先于派生类），让后面的计算全部命中编译器已有的实例化结果，嵌套深度变成常数。
template<class Self, size_t Step, class... Env>
struct warm_up : warm_up<Self, Step - 1, Env...>
{
	static constexpr size_t index = traits_of<Self>::shape.order[Step - 1];
	static constexpr bool done = warm_up<Self, Step - 1, Env...>::done
		&& sizeof(meta::tag<node_completions_t<Self, index, Env...>>) > 0;
};

template<class Self, class... Env>
struct warm_up<Self, 0, Env...>
{
	static constexpr bool done = true;
};

template<class Self, class... Env>
inline constexpr bool warmed_up = warm_up<Self, traits_of<Self>::count, Env...>::done;

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
inline constexpr bool stoppable_v = analysis<Self, probe_env<Self, Env>>::can_fail;

template<class Self, class Env>
using node_env_t = node_env<Self, Env, stoppable_v<Self, Env>>;

/// 从环境里的整图状态指针类型反推图的类型；不要求 graph_state 完整。
template<class Pointer>
struct graph_of;

template<class Self, class Env>
struct graph_of<graph_state<Self, Env>*>
{
	using type = Self;
	using env_type = Env;
};

template<class Env>
concept inside_graph = requires(Env const& env) { graph_detail::get_graph_state(env); };

template<class Env>
	requires inside_graph<Env>
using graph_of_env_t = typename graph_of<decltype(graph_detail::get_graph_state(::std::declval<Env const&>()))>::type;

/// 输入 sender 在给定接收者环境下的完成签名：从环境里拿到图类型，再按前驱算。
template<class Keys, class Env>
	requires inside_graph<Env>
[[nodiscard]] consteval auto input_completions_from_env();

/// 有接收者环境时的完成签名：先按探测环境、再按最终节点环境检查每个节点。
template<class Self, class Env>
[[nodiscard]] consteval auto completions_with_env()
{
	static_assert(warmed_up<Self> && warmed_up<Self, probe_env<Self, Env>>);
	if constexpr (!all_nodes_validity<Self, probe_env<Self, Env>>::value)
	{
		return all_nodes_validity<Self, probe_env<Self, Env>>::first_invalid();
	}
	else if constexpr (!(warmed_up<Self, node_env_t<Self, Env>> && all_nodes_validity<Self, node_env_t<Self, Env>>::value))
	{
		return all_nodes_validity<Self, node_env_t<Self, Env>>::first_invalid();
	}
	else
	{
		return typename analysis<Self, node_env_t<Self, Env>>::completions{};
	}
}

/// 整图 sender 的完成签名；节点不合法时把不合法的结果原样透传给 stdexec 诊断。
template<class Self, class... Env>
[[nodiscard]] consteval auto completions_of()
{
	if constexpr (sizeof...(Env) == 0)
	{
		// 输入 sender 依赖环境，所以整图在无环境下必然是 dependent sender。
		return ::stdexec::__throw_dependent_sender_error<::std::remove_cvref_t<Self>>();
	}
	else
	{
		return graph_detail::completions_with_env<Self, Env...>();
	}
}

// ---------------------------------------------------------------- 输入 sender 的完成签名

template<bool Shared>
struct accessor
{
	/// 共享的值按 const& 交给后继；独占的值 move 过去（签名写成按值）。
	template<class V>
	using fn = ::std::conditional_t<Shared, V const&, V>;
};

template<class Self, class Dependencies, class... Env>
struct input_values;

/// 只看前驱节点的完成签名，不触发整图分析——否则会沿着"节点 -> 输入 sender -> 整图"递归实例化。
template<class Self, size_t... D, class... Env>
struct input_values<Self, ::std::index_sequence<D...>, Env...>
{
	static_assert(((D < traits_of<Self>::count) && ...), "sdag: 输入 sender 引用了图里不存在的键");

	template<size_t J>
	using node = node_summary<node_completions_t<Self, J, Env...>>;

	static constexpr bool has_values = (node<D>::has_values && ...);

	using type = meta::concat_t<
		meta::transform_t<accessor<shared_v<Self, D>>::template fn, typename node<D>::values>...>;

	using completions = ::std::conditional_t<has_values,
		::stdexec::completion_signatures<sig::value_signature_t<type>>,
		::stdexec::completion_signatures<>>;
};

template<class Self, class Keys>
using dependencies_of_keys_t = keys_to_indices_t<typename traits_of<Self>::data::keys, Keys>;

template<class Self, class Keys, class... Env>
[[nodiscard]] consteval auto input_completions()
{
	using dependencies = dependencies_of_keys_t<Self, Keys>;
	using checks = validity<Self, dependencies, Env...>;
	if constexpr (!checks::value)
	{
		return checks::first_invalid();
	}
	else
	{
		return typename input_values<Self, dependencies, Env...>::completions{};
	}
}

template<class Keys, class Env>
	requires inside_graph<Env>
[[nodiscard]] consteval auto input_completions_from_env()
{
	return graph_detail::input_completions<graph_of_env_t<Env>, Keys, Env>();
}

struct _THE_INPUT_SENDER_OF_A_DAG_NODE_WAS_CONNECTED_OUTSIDE_OF_A_DAG_;

template<class Keys, class Receiver>
struct input_operation
{
	Receiver _receiver;

	void start() noexcept
	{
		auto* state = graph_detail::get_graph_state(::stdexec::get_env(_receiver));
		state->template deliver<Keys>(::std::move(_receiver));
	}
};

// ---------------------------------------------------------------- 整图状态：值槽

template<class Values>
struct slot_type;

template<class... Vs>
struct slot_type<meta::type_list<Vs...>>
{
	using type = ::std::optional<::std::tuple<Vs...>>;
};

template<class Self, class Env, class Indices>
struct graph_state_impl;

template<class Self, class Env, size_t... I>
struct graph_state_impl<Self, Env, ::std::index_sequence<I...>>
{
	static_assert(warmed_up<Self, probe_env<Self, Env>>);
	static_assert(all_nodes_validity<Self, probe_env<Self, Env>>::value, "sdag: 有节点在接收者环境下无法给出完成签名");
	static_assert(warmed_up<Self, node_env_t<Self, Env>>);
	static_assert(all_nodes_validity<Self, node_env_t<Self, Env>>::value, "sdag: 有节点在整图环境下无法给出完成签名");

	using traits = traits_of<Self>;
	static constexpr auto shape = traits::shape;
	static constexpr size_t count = traits::count;
	static constexpr bool stoppable = stoppable_v<Self, Env>;
	using env_type = node_env_t<Self, Env>;
	using analysis_type = analysis<Self, env_type>;
	using completions = typename analysis_type::completions;

	template<size_t J>
	using values_t = typename analysis_type::template values_t<J>;

	template<size_t J>
	static constexpr bool slot_needed = analysis_type::template slot_needed<J>;

	template<size_t J>
	using slot_t = ::std::conditional_t<slot_needed<J>, typename slot_type<values_t<J>>::type, meta::empty>;

	SDAG_NO_UNIQUE_ADDRESS meta::indexed_tuple<slot_t<I>...> _slots{};
};

/// 节点值的存放处；fused_core 从它派生，节点环境里只暴露这一层。
template<class Self, class Env>
struct graph_state : graph_state_impl<Self, Env, ::std::make_index_sequence<traits_of<Self>::count>>
{
	using base = graph_state_impl<Self, Env, ::std::make_index_sequence<traits_of<Self>::count>>;
	using base::shape;
	using typename base::analysis_type;

	/// input_operation 启动时调用：把 Keys 对应前驱的值交给接收者。
	template<class Keys, class Receiver>
	void deliver(Receiver&& receiver) noexcept
	{
		using dependencies = dependencies_of_keys_t<Self, Keys>;
		// 接收者那条链路是在它自己的环境下算出输入签名的；必须与这里存的槽位一致，
		// 否则说明某个节点 sender 在不同环境下报告了不同的值类型。
		using declared = decltype(graph_detail::input_completions<Self, Keys, ::stdexec::env_of_t<Receiver>>());
		using stored = decltype(graph_detail::input_completions<Self, Keys, typename base::env_type>());
		static_assert(::std::same_as<declared, stored>, "sdag: 节点在接收者环境与整图环境下报告的值类型不一致");
		if constexpr (!input_values<Self, dependencies, typename base::env_type>::has_values)
		{
			// 某个前驱永远不会带值结束，这个节点也就永远不会被启动。
			::std::terminate();
		}
		else
		{
			deliver_impl(dependencies{}, ::std::forward<Receiver>(receiver));
		}
	}

	template<::std::size_t D, ::std::size_t... Rest, class Receiver, class... Accumulated>
	void deliver_impl(::std::index_sequence<D, Rest...>, Receiver&& receiver, Accumulated&&... accumulated) noexcept
	{
		if constexpr (base::template values_t<D>::size == 0)
		{
			deliver_impl(::std::index_sequence<Rest...>{}, ::std::forward<Receiver>(receiver), ::std::forward<Accumulated>(accumulated)...);
		}
		else
		{
			::std::apply(
				[&](auto&... values) noexcept
				{
					if constexpr (shared_v<Self, D>)
					{
						deliver_impl(::std::index_sequence<Rest...>{}, ::std::forward<Receiver>(receiver),
							::std::forward<Accumulated>(accumulated)..., ::std::as_const(values)...);
					}
					else
					{
						deliver_impl(::std::index_sequence<Rest...>{}, ::std::forward<Receiver>(receiver),
							::std::forward<Accumulated>(accumulated)..., ::std::move(values)...);
					}
				},
				*meta::get<D>(this->_slots));
		}
	}

	template<class Receiver, class... Accumulated>
	void deliver_impl(::std::index_sequence<>, Receiver&& receiver, Accumulated&&... accumulated) noexcept
	{
		::stdexec::set_value(::std::forward<Receiver>(receiver), ::std::forward<Accumulated>(accumulated)...);
	}
};

}

// ---------------------------------------------------------------- 输入 sender

template<class Keys>
struct input_sender
{
	using sender_concept = ::stdexec::sender_t;
	using keys = Keys;

	[[nodiscard]] static constexpr graph_detail::input_attrs get_env() noexcept
	{
		return {};
	}

	template<class Self, class... Env>
	[[nodiscard]] static consteval auto get_completion_signatures()
	{
		if constexpr (sizeof...(Env) == 0)
		{
			return ::stdexec::__throw_dependent_sender_error<Self>();
		}
		else if constexpr (!(graph_detail::inside_graph<Env> && ...))
		{
			return ::exec::throw_compile_time_error<graph_detail::_THE_INPUT_SENDER_OF_A_DAG_NODE_WAS_CONNECTED_OUTSIDE_OF_A_DAG_>();
		}
		else
		{
			return graph_detail::input_completions_from_env<Keys, Env...>();
		}
	}

	template<::stdexec::receiver Receiver>
	[[nodiscard]] constexpr graph_detail::input_operation<Keys, Receiver> connect(Receiver receiver) const
		noexcept(::std::is_nothrow_move_constructible_v<Receiver>)
	{
		return {::std::move(receiver)};
	}
};

// ---------------------------------------------------------------- 降解入口（定义在 composed.h）

/// lower(graph, fanin, fanout)：把 dag_t sexpr 降解为 fanin / fanout / let_value 组合的普通 sender 树。
struct lower_t
{
	template<class Graph, class FanIn, class FanOut>
	[[nodiscard]] auto operator()(Graph&& graph, FanIn const& fanin, FanOut const& fanout) const;
};

inline constexpr lower_t lower{};

// ---------------------------------------------------------------- 算法标签

namespace fused_detail
{

template<class Self, class Receiver>
struct fused_operation;

}

namespace graph_detail
{

/// 节点 I 的 sender：载荷是 sender 就原样取出，否则把输入 sender 喂给它。
template<class Node>
[[nodiscard]] constexpr auto make_child(Node&& node)
{
	using keys = typename node_dependencies_t<Node>::keys;
	using payload_type = decltype(::sdag::payload(::std::forward<Node>(node)));
	static_assert(node_payload<payload_type, input_sender<keys>>,
		"sdag: 节点载荷既不是 sender，也不是能接受输入 sender 的适配器（例如 ::stdexec::then(f)）");
	if constexpr (sender_payload<payload_type>)
	{
		static_assert(!contains_input_v<::std::decay_t<payload_type>>, "sdag: 只有控制依赖的节点 sender 里不能出现输入 sender");
		return ::std::decay_t<payload_type>(::sdag::payload(::std::forward<Node>(node)));
	}
	else
	{
		using child = decltype(::sdag::payload(::std::forward<Node>(node))(input_sender<keys>{}));
		static_assert(input_count_v<child> == 1,
			"sdag: 节点适配器必须把输入 sender 恰好用一次（它的值只会被交付一次，repeat / retry 这类会重启子 sender 的适配器不能套在输入上）");
		return ::sdag::payload(::std::forward<Node>(node))(input_sender<keys>{});
	}
}

template<class Self, class Indices>
struct children_at;

template<class Self, size_t... I>
struct children_at<Self, ::std::index_sequence<I...>>
{
	using type = meta::type_list<typename traits_of<Self>::template child_t<I>...>;
};

/// 汇点的子 sender 类型列表（声明顺序）。
template<class Self>
using sink_children_t = typename children_at<Self, meta::index_sequence_of<sink_indices<traits_of<Self>::shape>>>::type;

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

template<class Self, class Env, class Indices>
struct when_all_probe;

/// 探针：when_all<汇点 sender（输入叶子换成占位 sender）...>。保留汇点的属性（完成调度器等），
/// 签名与真实降解后的汇点一致，又不需要整图环境就能被 domain 的 transform_sender 检查。
template<class Self, class Env, size_t... S>
struct when_all_probe<Self, Env, ::std::index_sequence<S...>>
{
	template<size_t J>
	using keys_t = typename meta::at_t<J, typename traits_of<Self>::data::dependencies>::keys;

	template<size_t J>
	using placeholder_t = placeholder_sender<decltype(graph_detail::input_completions<Self, keys_t<J>, node_env_t<Self, Env>>())>;

	template<size_t J>
	using probe_child_t = substituted_t<typename traits_of<Self>::template child_t<J>, placeholder_t<J>>;

	/// 真实的 when_all 至少两个子 sender；只有一个汇点时把它重复一次，免得 domain 按子节点数约束时漏判。
	static consteval auto compute() noexcept
	{
		if constexpr (sizeof...(S) == 1)
		{
			return meta::tag<::stdexec::__sexpr<::stdexec::when_all_t, ::stdexec::__, probe_child_t<S>..., probe_child_t<S>...>>{};
		}
		else
		{
			return meta::tag<::stdexec::__sexpr<::stdexec::when_all_t, ::stdexec::__, probe_child_t<S>...>>{};
		}
	}

	using type = typename decltype(compute())::type;
};

/// transform_sender 分两个阶段：start_t 用环境的起始 domain，set_value_t 用 sender 自己报告的完成域。
template<class OpTag, class Self, class Env>
[[nodiscard]] consteval auto phase_domain() noexcept
{
	if constexpr (::std::same_as<OpTag, ::stdexec::start_t>)
	{
		return meta::tag<::stdexec::__detail::__starting_domain_t<Env>>{};
	}
	else
	{
		return meta::tag<::stdexec::__detail::__completing_domain_t<void, Self, Env>>{};
	}
}

/// 当前阶段的 domain 会不会定制覆盖在汇点上的 when_all：会，整图就降解成标准算法交给它；
/// 不会（默认 domain、只定制 bulk 的线程池 domain 等）就保持融合。"定制"按 __default_domain_like 判断。
/// 节点签名不合法时直接返回 false，把诊断留给后面的 connect。
template<class OpTag, class Self, class Env>
[[nodiscard]] consteval bool lowering_wanted_in() noexcept
{
	if constexpr (!(warmed_up<Self, probe_env<Self, Env>> && all_nodes_validity<Self, probe_env<Self, Env>>::value))
	{
		return false;
	}
	else if constexpr (!(warmed_up<Self, node_env_t<Self, Env>> && all_nodes_validity<Self, node_env_t<Self, Env>>::value))
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
			using probe = typename when_all_probe<Self, Env, meta::index_sequence_of<sink_indices<traits_of<Self>::shape>>>::type;
			return !::stdexec::__default_domain_like<domain, OpTag, probe, Env>;
		}
	}
}

template<class OpTag, class Self, class Env>
inline constexpr bool lowering_wanted = graph_detail::lowering_wanted_in<OpTag, Self, Env>();

// ---------------------------------------------------------------- 属性（与 when_all 的 __attrs 同一套语义）

/// 完成域：值完成发生在最后一个汇点完成的地方——取汇点在整图节点环境下的公共完成域；
/// 错误 / 停止可能来自任一节点——取全部节点的公共域。完成行为取全部节点的合成。
template<class Data, class... Children>
struct attrs
{
	using self_type = ::stdexec::__sexpr<dag_t, Data, Children...>&&;
	static constexpr auto shape = Data::shape;

	template<class Env>
	static consteval bool evaluable_in() noexcept
	{
		if constexpr (!all_nodes_validity<self_type, probe_env<self_type, Env>>::value)
		{
			return false;
		}
		else
		{
			return all_nodes_validity<self_type, node_env_t<self_type, Env>>::value;
		}
	}

	template<class Env>
	static constexpr bool evaluable = evaluable_in<Env>();

	template<class Env>
	using child_env_t = node_env_t<self_type, Env>;

	/// 有节点不发 set_value 时没有 type，让下面的查询在立即上下文里被排除掉（与 when_all 一致）。
	template<class Env, class Nodes>
	struct common_domain
	{
	};

	template<class Env, class... Nodes>
		requires requires { typename ::stdexec::__common_domain_t<::stdexec::__completion_domain_of_t<::stdexec::set_value_t, Nodes, child_env_t<Env>>...>; }
	struct common_domain<Env, meta::type_list<Nodes...>>
	{
		using type = ::stdexec::__common_domain_t<::stdexec::__completion_domain_of_t<::stdexec::set_value_t, Nodes, child_env_t<Env>>...>;
	};

	template<class Env>
	using value_domain_t = typename common_domain<Env, sink_children_t<self_type>>::type;

	template<class Env>
	using any_node_domain_t = typename common_domain<Env, meta::type_list<Children...>>::type;

	template<class Env>
		requires evaluable<Env>
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_value_t>, Env const&) const noexcept
		-> value_domain_t<Env>;

	template<class Env>
		requires evaluable<Env>
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_error_t>, Env const&) const noexcept
		-> ::stdexec::__common_domain_t<value_domain_t<Env>, any_node_domain_t<Env>>;

	template<class Env>
		requires evaluable<Env>
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_stopped_t>, Env const&) const noexcept
		-> ::stdexec::__common_domain_t<value_domain_t<Env>, any_node_domain_t<Env>>;

	template<class Tag, class... Env>
	[[nodiscard]] constexpr auto query(::stdexec::__get_completion_behavior_t<Tag>, Env const&...) const noexcept
	{
		return ::stdexec::__completion_behavior::__common(::stdexec::__get_completion_behavior<Tag, Children, Env...>()...);
	}
};

template<class Data>
struct attrs<Data>
{
	template<class Tag>
	[[nodiscard]] constexpr auto query(::stdexec::__get_completion_behavior_t<Tag>) const noexcept
	{
		return ::stdexec::__completion_behavior::__inline_completion;
	}
};

}

/// 图算法的标签。dag_t{}(nodes...) 得到图 sender；dag() 是它的策略化包装。
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces" // __sexpr 的聚合初始化靠大括号省略，stdexec 自己也这样写
struct dag_t
{
	template<graph_node... Nodes>
	[[nodiscard]] constexpr auto operator()(Nodes&&... nodes) const
	{
		return ::stdexec::__sexpr{dag_t{},
			graph_detail::dag_data<meta::type_list<node_key_t<Nodes>...>, meta::type_list<node_dependencies_t<Nodes>...>>{},
			graph_detail::make_child(::std::forward<Nodes>(nodes))...};
	}

	/// 所在 domain 会定制 when_all 时，把整图降解为 when_all / split / let_value 组合，让它吃到定制；
	/// 否则（默认 domain、只定制 bulk 的线程池 domain 等）不声明此重载，走 fused.h 的融合 op-state。
	template<class OpTag, class Sender, class Env>
		requires graph_detail::lowering_wanted<OpTag, Sender&&, Env>
	[[nodiscard]] static auto transform_sender(OpTag, Sender&& sender, Env const&)
	{
		return ::sdag::lower(::std::forward<Sender>(sender), ::stdexec::when_all, ::exec::split);
	}
};

#pragma clang diagnostic pop

/// 图 sender 的形状（邻接矩阵、入出度、拓扑序）。
template<class Graph>
constexpr auto shape_v = graph_detail::traits_of<Graph&&>::shape;

}

// ---------------------------------------------------------------- sexpr 实现挂接

namespace STDEXEC
{

template<>
struct __sexpr_impl<::sdag::dag_t> : __sexpr_defaults
{
	static constexpr auto __get_attrs = []<class Data, class... Children>(__ignore, Data const&, Children const&...) noexcept
		-> ::sdag::graph_detail::attrs<Data, Children...>
	{
		return {};
	};

	template<class Self, class... Env>
	[[nodiscard]] static consteval auto __get_completion_signatures()
	{
		return ::sdag::graph_detail::completions_of<Self&&, Env...>();
	}

	/// 融合 op-state 见 fused.h；这里只声明返回类型，实例化时才需要它完整。
	static constexpr auto __connect = []<class Sender, class Receiver>(Sender&& sender, Receiver receiver)
		noexcept(::std::is_nothrow_constructible_v<::sdag::fused_detail::fused_operation<Sender&&, Receiver>, Sender&&, Receiver>)
		-> ::sdag::fused_detail::fused_operation<Sender&&, Receiver>
	{
		return ::sdag::fused_detail::fused_operation<Sender&&, Receiver>(::std::forward<Sender>(sender), ::std::move(receiver));
	};
};

}
