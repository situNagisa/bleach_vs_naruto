#pragma once

/// 融合策略（默认）：整张图是一个 sender，连接后是一个 operation state。
///
/// 扇出不走 split：每个节点的值存一份在整图 op-state 里，后继按引用读；
/// 扇入不走 when_all：入度 ≥ 2 的节点只有一个原子倒计数，入度 1 的节点由前驱直接启动。
/// 没有堆分配、没有引用计数、没有类型擦除；不会失败的图连停止源也不存在。
///
/// 完成语义与 when_all 一致：
///   - 全部成功：set_value(汇点 1 的值..., 汇点 2 的值..., ...)（汇点按声明顺序）
///   - 任一节点 set_error：请求停止、未启动的后继跳过、全部在飞节点结束后以第一个错误完成
///   - 任一节点 set_stopped：同上，以 set_stopped 完成（错误优先于停止）
///   - 值的传递：只有一个后继的前驱把值 move 给它；多个后继的前驱按 V const& 共享

#include <atomic>
#include <concepts>
#include <cstddef>
#include <exception>
#include <array>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <stdexec/execution.hpp>

#include "meta.h"
#include "signatures.h"
#include "node.h"
#include "shape.h"

namespace sdag
{

template<graph_node... Nodes>
struct fused_sender;

namespace fused_detail
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

// ---------------------------------------------------------------- sender 特征

template<class Sender>
struct sender_traits;

template<graph_node... Nodes>
struct sender_traits<fused_sender<Nodes...>>
{
	using nodes = meta::type_list<Nodes...>;
	static constexpr auto shape = fused_sender<Nodes...>::shape;
	static constexpr size_t count = sizeof...(Nodes);

	template<size_t I>
	using dependencies_t = dependency_indices_t<I, Nodes...>;
};

/// Self 永远是 fused_sender 的引用类型（X&& 或 X const&），由 fused_sender 规范化后传入。
template<class Self>
using traits_of = sender_traits<::std::remove_cvref_t<Self>>;

template<class Self, size_t I>
struct input_sender;

template<class Self, size_t I>
using payload_ref_t = decltype(::sdag::payload(::std::get<I>(::std::declval<Self>()._nodes)));

/// 节点 I 的 sender：载荷是 sender 就原样取出，否则把输入 sender 喂给它。
template<class Self, size_t I>
[[nodiscard]] constexpr auto make_node_sender(Self&& self)
{
	using payload_type = payload_ref_t<Self, I>;
	static_assert(node_payload<payload_type, input_sender<Self, I>>,
		"sdag: 节点载荷既不是 sender，也不是能接受输入 sender 的适配器（例如 ::stdexec::then(f)）");
	if constexpr (sender_payload<payload_type>)
	{
		return ::std::decay_t<payload_type>(::sdag::payload(::std::get<I>(::std::forward<Self>(self)._nodes)));
	}
	else
	{
		return ::sdag::payload(::std::get<I>(::std::forward<Self>(self)._nodes))(input_sender<Self, I>{});
	}
}

template<class Self, size_t I>
using node_sender_t = decltype(fused_detail::make_node_sender<Self, I>(::std::declval<Self>()));

/// 节点 I 是否消费前驱的值（适配器载荷消费，sender 载荷只有控制依赖）。
template<class Self, size_t I>
inline constexpr bool consumes_input = !sender_payload<payload_ref_t<Self, I>>;

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
	static constexpr bool nothrow_store =
		sig::all_nothrow_decay_copyable<raw_values>::value && sig::all_nothrow_decay_copyable<raw_errors>::value;
	static constexpr bool can_fail = errors::size != 0 || sends_stopped || !nothrow_store;
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

	static constexpr bool can_fail = (node<I>::can_fail || ...);
	static constexpr bool sends_stopped = (node<I>::sends_stopped || ...);
	static constexpr bool nothrow_store = (node<I>::nothrow_store && ...);
	/// 任一节点永远不会带值结束，整图就永远不会以 set_value 完成（每个节点都是某个汇点的祖先）。
	static constexpr bool has_values = (node<I>::has_values && ...);

	using error_types = meta::unique_t<meta::concat_t<
		typename node<I>::errors...,
		::std::conditional_t<nothrow_store, meta::type_list<>, meta::type_list<::std::exception_ptr>>>>;
	using value_types = meta::concat_t<::std::conditional_t<shape.is_sink(I), values_t<I>, meta::type_list<>>...>;
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

/// 节点 sender 的完成签名要先算前驱的：then(f) 套在输入 sender 上时 stdexec 会立刻检查它的
/// 完成签名，于是节点 N 的类型计算会嵌套进节点 N-1 的……每层几十个实例化帧，长链会撞上
/// 编译器的模板深度上限。这里按拓扑序逐个实例化（基类先于派生类），让后面的计算全部命中
/// 编译器已有的实例化结果，嵌套深度变成常数。
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
	[[no_unique_address]] ::std::conditional_t<Stoppable, ::stdexec::inplace_stop_token, meta::empty_for<node_env>> _stop_token;
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
		static_assert(warmed_up<Self>);
		if constexpr (!all_nodes_validity<Self>::value)
		{
			return all_nodes_validity<Self>::first_invalid();
		}
		else
		{
			return typename analysis<Self>::completions{};
		}
	}
	else
	{
		return fused_detail::completions_with_env<Self, Env...>();
	}
}

// ---------------------------------------------------------------- 输入 sender

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
	template<size_t J>
	using node = node_summary<node_completions_t<Self, J, Env...>>;

	static constexpr bool has_values = (node<D>::has_values && ...);

	using type = meta::concat_t<
		meta::transform_t<accessor<(traits_of<Self>::shape.out_degree[D] > 1)>::template fn, typename node<D>::values>...>;

	using completions = ::std::conditional_t<has_values,
		::stdexec::completion_signatures<sig::value_signature_t<type>>,
		::stdexec::completion_signatures<>>;
};

template<class Self, size_t I, class... Env>
[[nodiscard]] consteval auto input_completions()
{
	using dependencies = typename traits_of<Self>::template dependencies_t<I>;
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

template<class Self, size_t I, class Receiver>
struct input_operation
{
	Receiver _receiver;

	void start() noexcept
	{
		auto* state = fused_detail::get_graph_state(::stdexec::get_env(_receiver));
		state->template deliver<I>(::std::move(_receiver));
	}
};

/// 节点 I 的输入：启动时把前驱的值按 after<...> 的声明顺序一次性 set_value 给接收者。
/// 本身没有状态；前驱的值在整图状态里，通过接收者环境的 get_graph_state 查询找到。
template<class Self, size_t I>
struct input_sender
{
	using sender_concept = ::stdexec::sender_t;

	template<class InputSelf, class... Env>
	[[nodiscard]] static consteval auto get_completion_signatures()
	{
		return fused_detail::input_completions<Self, I, Env...>();
	}

	template<::stdexec::receiver Receiver>
	[[nodiscard]] constexpr input_operation<Self, I, Receiver> connect(Receiver receiver) const
		noexcept(::std::is_nothrow_move_constructible_v<Receiver>)
	{
		return {::std::move(receiver)};
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
	static_assert(warmed_up<Self> && warmed_up<Self, probe_env<Self, Env>>);
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

	/// 值没人读（既不是汇点、后继又都只是控制依赖）或者根本没有值时，不占存储。
	template<size_t J>
	static constexpr bool slot_needed = values_t<J>::size != 0
		&& (shape.is_sink(J) || ((shape.edge[J][I] && consumes_input<Self, I>) || ...));

	template<size_t J>
	using slot_t = ::std::conditional_t<slot_needed<J>, typename slot_type<values_t<J>>::type, meta::empty>;

	[[no_unique_address]] meta::indexed_tuple<slot_t<I>...> _slots{};
};

/// 节点值的存放处；fused_core 从它派生，节点环境里只暴露这一层。
template<class Self, class Env>
struct graph_state : graph_state_impl<Self, Env, ::std::make_index_sequence<traits_of<Self>::count>>
{
	using base = graph_state_impl<Self, Env, ::std::make_index_sequence<traits_of<Self>::count>>;
	using base::shape;
	using typename base::analysis_type;

	/// input_operation 启动时调用：把节点 I 的前驱值交给接收者。
	template<::std::size_t I, class Receiver>
	void deliver(Receiver&& receiver) noexcept
	{
		using dependencies = typename base::traits::template dependencies_t<I>;
		// 接收者那条链路是在它自己的环境下算出输入签名的；必须与这里存的槽位一致，
		// 否则说明某个节点 sender 在不同环境下报告了不同的值类型。
		using declared = decltype(fused_detail::input_completions<Self, I, ::stdexec::env_of_t<Receiver>>());
		using stored = decltype(fused_detail::input_completions<Self, I, typename base::env_type>());
		static_assert(::std::same_as<declared, stored>, "sdag: 节点在接收者环境与整图环境下报告的值类型不一致");
		if constexpr (!input_values<Self, dependencies, typename base::env_type>::has_values)
		{
			// 某个前驱永远不会带值结束，这个节点也就永远不会被启动。
			::std::terminate();
		}
		else
		{
			deliver_impl<I>(dependencies{}, ::std::forward<Receiver>(receiver));
		}
	}

	template<::std::size_t I, ::std::size_t D, ::std::size_t... Rest, class Receiver, class... Accumulated>
	void deliver_impl(::std::index_sequence<D, Rest...>, Receiver&& receiver, Accumulated&&... accumulated) noexcept
	{
		if constexpr (base::template values_t<D>::size == 0)
		{
			deliver_impl<I>(::std::index_sequence<Rest...>{}, ::std::forward<Receiver>(receiver), ::std::forward<Accumulated>(accumulated)...);
		}
		else
		{
			::std::apply(
				[&](auto&... values) noexcept
				{
					if constexpr (shape.out_degree[D] > 1)
					{
						deliver_impl<I>(::std::index_sequence<Rest...>{}, ::std::forward<Receiver>(receiver),
							::std::forward<Accumulated>(accumulated)..., ::std::as_const(values)...);
					}
					else
					{
						deliver_impl<I>(::std::index_sequence<Rest...>{}, ::std::forward<Receiver>(receiver),
							::std::forward<Accumulated>(accumulated)..., ::std::move(values)...);
					}
				},
				*meta::get<D>(this->_slots));
		}
	}

	template<::std::size_t I, class Receiver, class... Accumulated>
	void deliver_impl(::std::index_sequence<>, Receiver&& receiver, Accumulated&&... accumulated) noexcept
	{
		::stdexec::set_value(::std::forward<Receiver>(receiver), ::std::forward<Accumulated>(accumulated)...);
	}
};

// ---------------------------------------------------------------- 整图 operation state

template<size_t Initial>
struct counter
{
	::std::atomic<size_t> value{Initial};
};

enum class state_kind : unsigned char
{
	running,
	error,
	stopped,
};

struct state_cell
{
	::std::atomic<state_kind> value{state_kind::running};
};

template<class Core, size_t I>
struct node_receiver
{
	using receiver_concept = ::stdexec::receiver_t;

	Core* _core;

	template<class... Args>
	void set_value(Args&&... args) noexcept
	{
		_core->template node_set_value<I>(::std::forward<Args>(args)...);
	}

	template<class Error>
	void set_error(Error&& error) noexcept
	{
		_core->template node_set_error<I>(::std::forward<Error>(error));
	}

	void set_stopped() noexcept
	{
		_core->template node_set_stopped<I>();
	}

	[[nodiscard]] typename Core::node_env_type get_env() const noexcept
	{
		return _core->make_node_env();
	}
};

template<auto Shape>
constexpr auto sink_indices = meta::filtered_indices<Shape.count, [](size_t index) { return Shape.is_sink(index); }>;

template<auto Shape>
constexpr auto root_indices = meta::filtered_indices<Shape.count, [](size_t index) { return Shape.is_root(index); }>;

template<class Self, class Receiver>
struct fused_operation;

template<class Self, class Receiver, class Indices>
struct fused_core_impl;

/// 除节点 op-state 之外的全部状态与调度逻辑。节点接收者只指向这一层，
/// 这样它的类型在节点 op-state 类型（定义在派生类里）算出来之前就是完整的。
template<class Self, class Receiver, size_t... I>
struct fused_core_impl<Self, Receiver, ::std::index_sequence<I...>> : graph_state<Self, ::stdexec::env_of_t<Receiver>>
{
	using env_type = ::stdexec::env_of_t<Receiver>;
	using state_base = graph_state<Self, env_type>;
	using node_env_type = typename state_base::env_type;
	using analysis_type = typename state_base::analysis_type;
	using operation_type = fused_operation<Self, Receiver>;

	static constexpr auto shape = state_base::shape;
	static constexpr size_t count = state_base::count;
	static constexpr bool stoppable = state_base::stoppable;
	static constexpr bool sends_stopped = analysis_type::sends_stopped;
	static constexpr size_t sink_count = shape.sink_count();

	using error_types = typename analysis_type::error_types;
	static constexpr bool has_errors = error_types::size != 0;
	using error_variant = meta::rebind_t<::std::variant, meta::concat_t<meta::type_list<::std::monostate>, error_types>>;

	struct forward_stop
	{
		fused_core_impl* _core;

		void operator()() const noexcept
		{
			_core->on_outer_stop();
		}
	};

	using stop_callback_t = ::stdexec::stop_callback_for_t<::stdexec::stop_token_of_t<env_type>, forward_stop>;

	template<size_t J>
	using pending_t = ::std::conditional_t<(shape.in_degree[J] > 1), counter<shape.in_degree[J]>, meta::empty>;

	struct state_tag;
	struct stop_source_tag;
	struct on_stop_tag;
	struct error_tag;
	struct sinks_tag;

	Receiver _receiver;
	[[no_unique_address]] ::std::conditional_t<stoppable, state_cell, meta::empty_for<state_tag>> _state{};
	[[no_unique_address]] ::std::conditional_t<stoppable, ::stdexec::inplace_stop_source, meta::empty_for<stop_source_tag>> _stop_source{};
	[[no_unique_address]] ::std::conditional_t<stoppable, ::std::optional<stop_callback_t>, meta::empty_for<on_stop_tag>> _on_stop{};
	[[no_unique_address]] ::std::conditional_t<stoppable && has_errors, error_variant, meta::empty_for<error_tag>> _error{};
	[[no_unique_address]] meta::indexed_tuple<pending_t<I>...> _pending{};
	[[no_unique_address]] ::std::conditional_t<(sink_count > 1), counter<sink_count>, meta::empty_for<sinks_tag>> _sinks{};

	explicit fused_core_impl(Receiver&& receiver) noexcept(::std::is_nothrow_move_constructible_v<Receiver>)
		: _receiver(::std::move(receiver))
	{
	}

	fused_core_impl(fused_core_impl&&) = delete;

	[[nodiscard]] node_env_type make_node_env() noexcept
	{
		if constexpr (stoppable)
		{
			return {this, _stop_source.get_token(), ::stdexec::get_env(_receiver)};
		}
		else
		{
			return {this, {}, ::stdexec::get_env(_receiver)};
		}
	}

	// ------------------------------------------------------------ 节点完成

	template<size_t J, class... Args>
	void node_set_value(Args&&... args) noexcept
	{
		if constexpr (state_base::template slot_needed<J>)
		{
			using tuple_type = meta::rebind_t<::std::tuple, typename analysis_type::template values_t<J>>;
			static_assert(::std::is_constructible_v<tuple_type, Args&&...>, "sdag: 节点实际发出的值与它声明的完成签名不符");
			auto& slot = meta::get<J>(this->_slots);
			if constexpr (::std::is_nothrow_constructible_v<tuple_type, Args&&...>)
			{
				slot.emplace(::std::forward<Args>(args)...);
			}
			else
			{
				try
				{
					slot.emplace(::std::forward<Args>(args)...);
				}
				catch (...)
				{
					node_set_error<J>(::std::current_exception());
					return;
				}
			}
		}
		node_done<J>();
	}

	template<size_t J, class Error>
	void node_set_error(Error&& error) noexcept
	{
		static_assert(stoppable && has_errors);
		switch (_state.value.exchange(state_kind::error, ::std::memory_order_acq_rel))
		{
		case state_kind::running:
			_stop_source.request_stop();
			[[fallthrough]];
		case state_kind::stopped:
			// 第一个错误；之后的错误全部丢弃。
			store_error(::std::forward<Error>(error));
			break;
		case state_kind::error:
			break;
		}
		node_done<J>();
	}

	template<size_t J>
	void node_set_stopped() noexcept
	{
		static_assert(stoppable && sends_stopped);
		auto expected = state_kind::running;
		if (_state.value.compare_exchange_strong(expected, state_kind::stopped, ::std::memory_order_acq_rel))
		{
			_stop_source.request_stop();
		}
		node_done<J>();
	}

	template<class Error>
	void store_error(Error&& error) noexcept
	{
		using stored = ::std::decay_t<Error>;
		if constexpr (sig::nothrow_decay_copyable<Error>)
		{
			_error.template emplace<stored>(::std::forward<Error>(error));
		}
		else
		{
			try
			{
				_error.template emplace<stored>(::std::forward<Error>(error));
			}
			catch (...)
			{
				_error.template emplace<::std::exception_ptr>(::std::current_exception());
			}
		}
	}

	// ------------------------------------------------------------ 调度

	/// 节点 J 结束（成功、失败、停止或被跳过）：是汇点就向整图报到，否则通知后继。
	template<size_t J>
	void node_done() noexcept
	{
		if constexpr (shape.is_sink(J))
		{
			sink_arrive();
		}
		else
		{
			(notify_edge<J, I>(), ...);
		}
	}

	template<size_t From, size_t To>
	void notify_edge() noexcept
	{
		if constexpr (shape.edge[From][To])
		{
			if constexpr (shape.in_degree[To] == 1)
			{
				launch<To>();
			}
			else if (meta::get<To>(_pending).value.fetch_sub(1, ::std::memory_order_acq_rel) == 1)
			{
				launch<To>();
			}
		}
	}

	/// 前驱全部到齐。图已经失败 / 停止时不再启动，直接当作结束往下传。
	template<size_t J>
	void launch() noexcept
	{
		if constexpr (stoppable)
		{
			if (_state.value.load(::std::memory_order_acquire) != state_kind::running)
			{
				node_done<J>();
				return;
			}
		}
		::stdexec::start(meta::get<J>(static_cast<operation_type*>(this)->_operations));
	}

	void sink_arrive() noexcept
	{
		if constexpr (sink_count > 1)
		{
			if (_sinks.value.fetch_sub(1, ::std::memory_order_acq_rel) != 1)
			{
				return;
			}
		}
		finish();
	}

	/// 外层停止请求。图能以 stopped 完成时切换状态（后继跳过）；
	/// 否则只把请求转给节点，图仍以它们的值完成——与 when_all 一致。
	void on_outer_stop() noexcept
	{
		if constexpr (sends_stopped)
		{
			// 暂时把汇点计数抬高，避免 request_stop 同步完成整图后在回调里继续碰 this。
			if constexpr (sink_count > 1)
			{
				_sinks.value.fetch_add(1, ::std::memory_order_relaxed);
			}
			auto expected = state_kind::running;
			if (_state.value.compare_exchange_strong(expected, state_kind::stopped, ::std::memory_order_acq_rel))
			{
				_stop_source.request_stop();
			}
			if constexpr (sink_count > 1)
			{
				sink_arrive();
			}
		}
		else
		{
			_stop_source.request_stop();
		}
	}

	void finish() noexcept
	{
		if constexpr (stoppable)
		{
			_on_stop.reset();
			switch (_state.value.load(::std::memory_order_acquire))
			{
			case state_kind::error:
				if constexpr (has_errors)
				{
					emit_error();
					return;
				}
				else
				{
					::std::unreachable();
				}
			case state_kind::stopped:
				if constexpr (sends_stopped)
				{
					::stdexec::set_stopped(::std::move(_receiver));
					return;
				}
				else
				{
					::std::unreachable();
				}
			case state_kind::running:
				break;
			}
		}
		emit_values(meta::index_sequence_of<sink_indices<shape>>{});
	}

	void emit_error() noexcept
	{
		[this]<size_t... K>(::std::index_sequence<K...>)
		{
			(void)((_error.index() == K + 1
				? (::stdexec::set_error(::std::move(_receiver), ::std::get<K + 1>(::std::move(_error))), true)
				: false) || ...);
		}(::std::make_index_sequence<error_types::size>{});
	}

	template<size_t S, size_t... Rest, class... Accumulated>
	void emit_values(::std::index_sequence<S, Rest...>, Accumulated&&... accumulated) noexcept
	{
		if constexpr (analysis_type::template values_t<S>::size == 0)
		{
			emit_values(::std::index_sequence<Rest...>{}, ::std::forward<Accumulated>(accumulated)...);
		}
		else
		{
			::std::apply(
				[&](auto&... values) noexcept
				{
					emit_values(::std::index_sequence<Rest...>{}, ::std::forward<Accumulated>(accumulated)..., ::std::move(values)...);
				},
				*meta::get<S>(this->_slots));
		}
	}

	template<class... Accumulated>
	void emit_values(::std::index_sequence<>, Accumulated&&... accumulated) noexcept
	{
		::stdexec::set_value(::std::move(_receiver), ::std::forward<Accumulated>(accumulated)...);
	}
};

template<class Self, class Receiver>
using fused_core = fused_core_impl<Self, Receiver, ::std::make_index_sequence<traits_of<Self>::count>>;

template<class Self, class Receiver, class Indices>
struct fused_operation_impl;

template<class Self, class Receiver, size_t... I>
struct fused_operation_impl<Self, Receiver, ::std::index_sequence<I...>> : fused_core<Self, Receiver>
{
	using core = fused_core<Self, Receiver>;

	template<size_t J>
	using node_receiver_t = node_receiver<core, J>;

	template<size_t J>
	using node_operation_t = ::stdexec::connect_result_t<node_sender_t<Self, J>, node_receiver_t<J>>;

	using operations_t = meta::indexed_tuple<node_operation_t<I>...>;

	static constexpr bool nothrow_connect =
		(noexcept(::stdexec::connect(fused_detail::make_node_sender<Self, I>(::std::declval<Self>()), ::std::declval<node_receiver_t<I>>())) && ...);

	operations_t _operations;

	explicit fused_operation_impl(Self self, Receiver receiver)
		noexcept(::std::is_nothrow_move_constructible_v<Receiver> && nothrow_connect)
		: core(::std::move(receiver))
		, _operations(meta::from_factory,
			[&]
			{
				// 每个工厂只从 _nodes 里取自己那一个元素，右值 Self 下也不会重复移动。
				return ::stdexec::connect(fused_detail::make_node_sender<Self, I>(::std::forward<Self>(self)), node_receiver_t<I>{this});
			}...)
	{
	}

	void start() noexcept
	{
		if constexpr (core::count == 0)
		{
			::stdexec::set_value(::std::move(this->_receiver));
			return;
		}
		if constexpr (core::stoppable)
		{
			this->_on_stop.emplace(::stdexec::get_stop_token(::stdexec::get_env(this->_receiver)), typename core::forward_stop{this});
		}
		launch_roots(meta::index_sequence_of<root_indices<core::shape>>{});
	}

	template<size_t... R>
	void launch_roots(::std::index_sequence<R...>) noexcept
	{
		(this->template launch<R>(), ...);
	}
};

template<class Self, class Receiver>
struct fused_operation : fused_operation_impl<Self, Receiver, ::std::make_index_sequence<traits_of<Self>::count>>
{
	using fused_operation_impl<Self, Receiver, ::std::make_index_sequence<traits_of<Self>::count>>::fused_operation_impl;
};

}

// ---------------------------------------------------------------- sender

template<graph_node... Nodes>
struct fused_sender
{
	using sender_concept = ::stdexec::sender_t;

	static constexpr auto shape = checked_shape<Nodes...>();
	static constexpr ::std::size_t node_count = sizeof...(Nodes);

	::std::tuple<Nodes...> _nodes;

	template<class Self, class... Env>
	[[nodiscard]] static consteval auto get_completion_signatures()
	{
		return fused_detail::completions_of<Self&&, Env...>();
	}

	template<class Self, ::stdexec::receiver Receiver>
	[[nodiscard]] fused_detail::fused_operation<Self&&, Receiver> connect(this Self&& self, Receiver receiver)
		noexcept(::std::is_nothrow_constructible_v<fused_detail::fused_operation<Self&&, Receiver>, Self&&, Receiver>)
	{
		return fused_detail::fused_operation<Self&&, Receiver>(::std::forward<Self>(self), ::std::move(receiver));
	}
};

/// 默认策略：整图融合成一个 op-state。
struct fused
{
	template<graph_node... Nodes>
	[[nodiscard]] static constexpr fused_sender<::std::remove_cvref_t<Nodes>...> compose(Nodes&&... nodes)
		noexcept((::std::is_nothrow_constructible_v<::std::remove_cvref_t<Nodes>, Nodes> && ...))
	{
		return {::std::tuple<::std::remove_cvref_t<Nodes>...>(::std::forward<Nodes>(nodes)...)};
	}
};

}
