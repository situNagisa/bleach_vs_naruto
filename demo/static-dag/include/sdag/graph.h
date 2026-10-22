#pragma once

/// 图的 sender 形态：一个以 dag_t 为标签的 ::stdexec::__sexpr，data 是编译期的键 / 依赖表，
/// children 就是各节点的 sender（用户的 then / continues_on / let_value 表达式原样保留）。
///
/// 这样整张图就是 stdexec 生态里的普通算法节点：tag_of_t / sender_for / 子节点内省 /
/// transform_sender 定制都成立；节点 sender 连接时照常被所在 domain 变换。
///
/// 本文件只有类型层面的分析（全部用反射写成 consteval 函数）与节点环境；
/// 真正的 operation state 在 fused.h，向 when_all / split 组合的降解在 composed.h。

#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <meta>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>
#include <exec/completion_signatures.hpp>

#include "meta.h"
#include "signatures.h"
#include "node.h"
#include "shape.h"

namespace sdag
{

struct dag_t;

/// 节点的输入 sender（Dependency 就是它的 after<...>）：启动时把前驱的值一次性 set_value 给接收者。
/// 本身没有状态，完成签名由接收者环境里的整图状态类型决定——这是一个"依赖环境"的 sender，像 read_env。
template<class Dependency>
struct input_sender;

namespace graph_detail
{

using ::std::size_t;
using meta::info;
using meta::infos;

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

// ---------------------------------------------------------------- 静态数据

/// sexpr 的 data：只有类型，运行期是空对象。
template<class Keys, class Dependencies>
struct dag_data
{
	using keys = Keys;
	using dependencies = Dependencies;
	static constexpr auto shape = checked_shape_of_lists<Keys, Dependencies>();
	static constexpr size_t count = shape.count;

	template<size_t I>
	using dependencies_t = keys_to_indices_t<Keys, meta::at_t<I, Dependencies>>;
};

// ---------------------------------------------------------------- 图 sender 的组成部分（反射）

/// Self 永远是图 sender 的引用类型（X&& 或 X const&）；这里先剥掉引用与别名。
[[nodiscard]] consteval info graph_type(info self)
{
	return ::std::meta::remove_cvref(::std::meta::dealias(self));
}

[[nodiscard]] consteval info data_of(info self)
{
	return meta::arguments(graph_type(self))[1];
}

/// 子 sender（节点 sender）类型，按声明顺序。
[[nodiscard]] consteval infos children_of(info self)
{
	infos all = meta::arguments(graph_type(self));
	return infos(all.begin() + 2, all.end());
}

[[nodiscard]] consteval size_t count_of(info self)
{
	return children_of(self).size();
}

template<class Self>
using data_t = typename [: graph_detail::data_of(^^Self) :];

template<class Self>
inline constexpr auto shape_of_v = data_t<Self>::shape;

template<class Self, size_t I>
using child_t = typename [: graph_detail::children_of(^^Self)[I] :];

/// 节点 I 的 sender，带上 Self 的值类别：右值图 move 子 sender，const& 图拷贝它们。
template<class Self, size_t I>
using node_sender_t = ::stdexec::__copy_cvref_t<Self, child_t<Self, I>>;

template<class Self, size_t I>
using dependencies_t = typename data_t<Self>::template dependencies_t<I>;

/// 节点 I 在图里的前驱索引（after<...> 声明顺序）。
template<class Self>
[[nodiscard]] consteval ::std::vector<size_t> dependency_indices(info dependency)
{
	return indices_of(meta::arguments(dependency), meta::arguments(^^typename data_t<Self>::keys));
}

// ---------------------------------------------------------------- 节点 sender 里的输入叶子（反射）

template<class T>
using sexpr_children_alias = typename T::__children_t;

template<class T>
using sexpr_tag_alias = typename T::__tag_t;

template<class T>
using sexpr_data_alias = typename T::__data_t;

/// 所有对 sender 类型的检查都先做这一步：去别名、去 cv / 引用。
[[nodiscard]] consteval info plain(info type)
{
	return ::std::meta::remove_cvref(::std::meta::dealias(type));
}

/// stdexec 算法 sender（含它们的派生类型）的子 sender；不是 sexpr 的类型给出空列表。
[[nodiscard]] consteval infos sexpr_children(info sender)
{
	sender = plain(sender);
	if (!::std::meta::can_substitute(^^sexpr_children_alias, {sender}))
	{
		return {};
	}
	return meta::arguments(::std::meta::substitute(^^sexpr_children_alias, {sender}));
}

[[nodiscard]] consteval bool is_input(info sender)
{
	sender = plain(sender);
	return ::std::meta::has_template_arguments(sender) && ::std::meta::template_of(sender) == ^^input_sender;
}

/// 节点 sender 里输入叶子的个数（递归穿过 stdexec 的算法层）。
[[nodiscard]] consteval size_t input_count(info sender)
{
	sender = plain(sender);
	if (is_input(sender))
	{
		return 1;
	}
	size_t total = 0;
	for (info child : sexpr_children(sender))
	{
		total += input_count(child);
	}
	return total;
}

template<class T>
inline constexpr size_t input_count_v = graph_detail::input_count(^^T);

/// 节点 I 是否消费前驱的值：含输入叶子的节点消费，纯 sender 载荷的节点只有控制依赖。
[[nodiscard]] consteval bool consumes_input(info self, size_t index)
{
	return input_count(children_of(self)[index]) != 0;
}

/// 读节点 D 的值的后继个数（只有控制依赖的后继不算）。
template<class Self>
[[nodiscard]] consteval size_t consumer_count(size_t from)
{
	size_t total = 0;
	for (size_t to : shape_of_v<Self>.successors(from))
	{
		total += consumes_input(^^Self, to) ? 1 : 0;
	}
	return total;
}

/// 节点 D 的值要不要共享：读它的值的后继超过一个才共享，否则 move 给唯一的消费者。
template<class Self, size_t D>
inline constexpr bool shared_v = graph_detail::consumer_count<Self>(D) > 1;

// ---------------------------------------------------------------- 把输入叶子换成别的 sender（只算类型）

/// 沿 stdexec 的 sexpr 层层重建：标签、data 不变，含输入叶子的子树递归替换，其余子树原样保留。
/// 重建出的是裸 __sexpr<Tag, Data, Child...>，与算法自己的派生 sender 类型共用同一个 __sexpr_impl<Tag>。
/// （input_count 只穿过 sexpr 层去数叶子，所以数到叶子的 sender 一定是 sexpr：这里不会遇到不透明层。）
[[nodiscard]] consteval info substituted(info sender, info input)
{
	sender = plain(sender);
	if (is_input(sender))
	{
		return ::std::meta::decay(input);
	}
	if (input_count(sender) == 0)
	{
		return sender;
	}
	infos rebuilt{
		::std::meta::dealias(::std::meta::substitute(^^sexpr_tag_alias, {sender})),
		::std::meta::decay(::std::meta::substitute(^^sexpr_data_alias, {sender})),
	};
	for (info child : sexpr_children(sender))
	{
		rebuilt.push_back(substituted(child, input));
	}
	return ::std::meta::substitute(^^::stdexec::__sexpr, rebuilt);
}

template<class CvSender, class Input>
using substituted_t = typename [: graph_detail::substituted(^^CvSender, ^^Input) :];

/// 输入 sender（与探针占位 sender）在 start 里内联完成。
struct input_attrs
{
	template<class Tag>
	[[nodiscard]] static constexpr auto query(::stdexec::__get_completion_behavior_t<Tag>) noexcept
	{
		return ::stdexec::__completion_behavior::__inline_completion;
	}
};

// ---------------------------------------------------------------- 完成签名分析（反射 + 展开语句）

template<class Self, size_t I, class... Env>
using node_completions_t = decltype(::stdexec::get_completion_signatures<node_sender_t<Self, I>, Env...>());

[[nodiscard]] consteval size_t value_completion_count(info completions)
{
	return sig::summarize(completions).value_lists.size();
}

/// libstdc++ 的 std::array<T, 0> 没有 consteval 的 operator[]，装不了 consteval-only 的 info；空图时留一个占位元素。
template<size_t N>
using info_array = ::std::array<info, N == 0 ? 1 : N>;

/// 整图在某个节点环境下的分析结果；全部是字面量，可以做 static constexpr 成员。
template<size_t N>
struct analysis_result
{
	/// 有节点算不出完成签名时为假，first_invalid 是那个结果类型（依赖环境的错误对象等），原样透传给 stdexec。
	bool valid = true;
	info first_invalid = ^^void;

	::std::array<bool, N> has_values{};
	::std::array<bool, N> sends_stopped{};
	::std::array<bool, N> nothrow_values{};
	::std::array<bool, N> nothrow_errors{};
	/// 值要不要存进整图 op-state：既不是要存值的汇点、后继又都只是控制依赖，或根本没有值时不存。
	::std::array<bool, N> slot_needed{};
	/// 每个节点：原始值类型列表、衰变后的值类型列表、衰变后的错误类型列表（都是 type_list）。
	info_array<N> raw_values{};
	info_array<N> values{};
	info_array<N> errors{};

	/// 只有一个汇点时它的值不存槽位：完成时直接转发给整图的接收者（与 then 一样不物化）。
	bool direct_sink = false;
	size_t sink_index = 0;
	bool any_stopped = false;
	bool has_errors = false;
	/// 任一节点永远不会带值结束，整图就永远不会以 set_value 完成（每个节点都是某个汇点的祖先）。
	bool all_has_values = true;
	/// 存值 / 存错误会不会抛：不存槽位的值不算。
	bool nothrow_store = true;
	bool can_fail = false;
	/// 整图的错误类型列表与值类型列表（type_list）。
	info error_types = ^^void;
	info value_types = ^^void;
};

/// 按拓扑序逐个节点取完成签名并汇总。按拓扑序走还顺带解决了嵌套实例化深度：
/// then(f) 问子 sender 的签名时，节点 N 的计算会嵌套进节点 N-1 的；先算前驱，后面的计算全部命中编译器已有的实例化结果。
template<class Self, class... Env>
[[nodiscard]] consteval auto analyze()
{
	constexpr auto shape = shape_of_v<Self>;
	analysis_result<shape.count> result;

	// 范围必须是常量表达式：用静态成员，不用上面的局部变量。
	template for (constexpr size_t index : shape_of_v<Self>.order)
	{
		using completions = node_completions_t<Self, index, Env...>;
		if constexpr (!sig::is_completion_signatures_v<completions>)
		{
			if (result.valid)
			{
				result.valid = false;
				result.first_invalid = ::std::meta::dealias(^^completions);
			}
		}
		else
		{
			static_assert(value_completion_count(^^completions) <= 1,
				"sdag: 每个节点至多只能有一种 set_value 完成；有多种时请先 ::stdexec::into_variant");
			sig::summary const summary = sig::summarize(^^completions);
			result.has_values[index] = !summary.value_lists.empty();
			result.raw_values[index] = result.has_values[index] ? summary.value_lists[0] : meta::list({});
			result.values[index] = meta::list(meta::map(^^::std::decay_t, meta::arguments(result.raw_values[index])));
			result.errors[index] = meta::list(meta::map(^^::std::decay_t, summary.error_types));
			result.sends_stopped[index] = summary.sends_stopped;
			result.nothrow_values[index] = sig::all_nothrow_decay_copyable(meta::arguments(result.raw_values[index]));
			result.nothrow_errors[index] = sig::all_nothrow_decay_copyable(summary.error_types);
		}
	}
	if (!result.valid)
	{
		return result;
	}

	result.direct_sink = shape.sink_count() == 1;
	result.sink_index = result.direct_sink ? shape.sinks()[0] : 0;
	for (size_t index = 0; index < shape.count; ++index)
	{
		bool consumed = false;
		for (size_t to : shape.successors(index))
		{
			consumed = consumed || consumes_input(^^Self, to);
		}
		result.slot_needed[index] = meta::arguments(result.values[index]).size() != 0
			&& ((shape.is_sink(index) && !result.direct_sink) || consumed);
		result.any_stopped = result.any_stopped || result.sends_stopped[index];
		result.has_errors = result.has_errors || meta::arguments(result.errors[index]).size() != 0;
		result.all_has_values = result.all_has_values && result.has_values[index];
		result.nothrow_store = result.nothrow_store && result.nothrow_errors[index] && (!result.slot_needed[index] || result.nothrow_values[index]);
	}
	result.can_fail = result.has_errors || result.any_stopped || !result.nothrow_store;

	::std::vector<infos> error_lists;
	for (size_t index = 0; index < shape.count; ++index)
	{
		error_lists.push_back(meta::arguments(result.errors[index]));
	}
	if (!result.nothrow_store)
	{
		error_lists.push_back({sig::exception_ptr_type()});
	}
	result.error_types = meta::list(meta::unique(meta::concat(error_lists)));

	if (result.direct_sink)
	{
		result.value_types = result.raw_values[result.sink_index];
	}
	else
	{
		::std::vector<infos> value_lists;
		for (size_t sink : shape.sinks())
		{
			value_lists.push_back(meta::arguments(result.values[sink]));
		}
		result.value_types = meta::list(meta::concat(value_lists));
	}
	return result;
}

template<class Self, class... Env>
struct analysis
{
	static constexpr auto result = graph_detail::analyze<Self, Env...>();

	template<size_t J>
	using values_t = typename [: result.values[J] :];

	using first_invalid_t = typename [: result.first_invalid :];

	/// 类的成员别名在类实例化时就会求值，所以不合法时给 void，别让 make_completions 在 ^^void 上抛异常。
	using completions = typename [: result.valid
		? sig::make_completions(result.all_has_values, result.value_types, result.error_types, result.any_stopped)
		: ^^void :];
};

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
inline constexpr bool stoppable_v = analysis<Self, probe_env<Self, Env>>::result.can_fail;

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

/// 有接收者环境时的完成签名：先按探测环境、再按最终节点环境检查每个节点。
template<class Self, class Env>
[[nodiscard]] consteval auto completions_with_env()
{
	using probe = analysis<Self, probe_env<Self, Env>>;
	if constexpr (!probe::result.valid)
	{
		return typename probe::first_invalid_t{};
	}
	else
	{
		using final = analysis<Self, node_env_t<Self, Env>>;
		if constexpr (!final::result.valid)
		{
			return typename final::first_invalid_t{};
		}
		else
		{
			return typename final::completions{};
		}
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

/// 共享的值按 const& 交给后继；独占的值 move 过去（签名写成按值）。
[[nodiscard]] consteval info access_type(info value, bool shared)
{
	return shared ? ::std::meta::add_lvalue_reference(::std::meta::add_const(value)) : value;
}

/// 只看前驱节点的完成签名，不触发整图分析——否则会沿着"节点 -> 输入 sender -> 整图"递归实例化。
/// 返回 completion_signatures<set_value_t(...)>；某个前驱永远不会带值结束时返回 completion_signatures<>；
/// 前驱签名算不出来时原样返回那个结果类型。
template<class Self, class Dependency, class... Env>
[[nodiscard]] consteval info input_completions_type()
{
	infos values;
	bool has_values = true;
	template for (constexpr size_t from : ::std::define_static_array(graph_detail::dependency_indices<Self>(^^Dependency)))
	{
		static_assert(from < count_of(^^Self), "sdag: 输入 sender 引用了图里不存在的键");
		using completions = node_completions_t<Self, from, Env...>;
		if constexpr (!sig::is_completion_signatures_v<completions>)
		{
			return ::std::meta::dealias(^^completions);
		}
		else
		{
			static_assert(value_completion_count(^^completions) <= 1,
				"sdag: 每个节点至多只能有一种 set_value 完成；有多种时请先 ::stdexec::into_variant");
			sig::summary const summary = sig::summarize(^^completions);
			if (summary.value_lists.empty())
			{
				has_values = false;
			}
			else
			{
				for (info value : meta::map(^^::std::decay_t, meta::arguments(summary.value_lists[0])))
				{
					values.push_back(access_type(value, shared_v<Self, from>));
				}
			}
		}
	}
	if (!has_values)
	{
		return ^^::stdexec::completion_signatures<>;
	}
	return ::std::meta::substitute(^^::stdexec::completion_signatures, {sig::make_signature(^^::stdexec::set_value_t, values)});
}

template<class Self, class Dependency, class... Env>
[[nodiscard]] consteval auto input_completions()
{
	return typename [: graph_detail::input_completions_type<Self, Dependency, Env...>() :]{};
}

template<class Dependency, class Env>
	requires inside_graph<Env>
[[nodiscard]] consteval auto input_completions_from_env()
{
	return graph_detail::input_completions<graph_of_env_t<Env>, Dependency, Env>();
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

// ---------------------------------------------------------------- 整图状态：值槽

/// 每个节点的值槽类型：optional<tuple<值...>> 或 meta::empty。
template<class Self, class Env>
[[nodiscard]] consteval info slot_types()
{
	constexpr auto& result = analysis<Self, Env>::result;
	infos slots;
	for (size_t index = 0; index < count_of(^^Self); ++index)
	{
		if (result.slot_needed[index])
		{
			slots.push_back(::std::meta::substitute(^^::std::optional,
				{::std::meta::substitute(^^::std::tuple, meta::arguments(result.values[index]))}));
		}
		else
		{
			slots.push_back(^^meta::empty);
		}
	}
	return meta::list(slots);
}

/// 交付表的一项：从哪个节点的槽位取第几个值，按 const& 共享还是 move。
struct slot_access
{
	size_t node;
	size_t element;
	bool shared;
};

/// 把一组节点的值按顺序铺平成交付表；allow_shared 为假时一律 move（汇点交给整图接收者）。
template<class Self, class Env>
[[nodiscard]] consteval ::std::vector<slot_access> delivery_plan(::std::vector<size_t> const& nodes, bool allow_shared)
{
	::std::vector<slot_access> plan;
	for (size_t node : nodes)
	{
		size_t const elements = meta::arguments(analysis<Self, Env>::result.values[node]).size();
		for (size_t element = 0; element < elements; ++element)
		{
			plan.push_back({node, element, allow_shared && consumer_count<Self>(node) > 1});
		}
	}
	return plan;
}

/// 节点值的存放处；fused_core 从它派生，节点环境里只暴露这一层。
template<class Self, class Env>
struct graph_state
{
	static_assert(analysis<Self, probe_env<Self, Env>>::result.valid, "sdag: 有节点在接收者环境下无法给出完成签名");
	static_assert(analysis<Self, node_env_t<Self, Env>>::result.valid, "sdag: 有节点在整图环境下无法给出完成签名");

	static constexpr auto shape = shape_of_v<Self>;
	static constexpr size_t count = shape.count;
	static constexpr bool stoppable = stoppable_v<Self, Env>;
	using env_type = node_env_t<Self, Env>;
	using analysis_type = analysis<Self, env_type>;
	static constexpr auto& result = analysis_type::result;
	using completions = typename analysis_type::completions;

	template<size_t J>
	using values_t = typename analysis_type::template values_t<J>;

	template<size_t J>
	static constexpr bool slot_needed = result.slot_needed[J];

	SDAG_NO_UNIQUE_ADDRESS meta::indexed_tuple_of<typename [: graph_detail::slot_types<Self, env_type>() :]> _slots{};

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

	template<class Dependency>
	static constexpr size_t delivery_size = delivery_plan<Self, env_type>(dependency_indices<Self>(^^Dependency), true).size();

	/// Dependency（after<...>）对应前驱的值，按声明顺序铺平。
	template<class Dependency>
	static constexpr auto delivery_table =
		meta::to_array<delivery_size<Dependency>>(delivery_plan<Self, env_type>(dependency_indices<Self>(^^Dependency), true));

	/// input_operation 启动时调用：把 Dependency 对应前驱的值交给接收者。
	template<class Dependency, class Receiver>
	void deliver(Receiver&& receiver) noexcept
	{
		// 接收者那条链路是在它自己的环境下算出输入签名的；必须与这里存的槽位一致，
		// 否则说明某个节点 sender 在不同环境下报告了不同的值类型。
		constexpr info declared = graph_detail::input_completions_type<Self, Dependency, ::stdexec::env_of_t<Receiver>>();
		constexpr info stored = graph_detail::input_completions_type<Self, Dependency, env_type>();
		static_assert(meta::same(declared, stored), "sdag: 节点在接收者环境与整图环境下报告的值类型不一致");
		if constexpr (meta::same(stored, ^^::stdexec::completion_signatures<>))
		{
			// 某个前驱永远不会带值结束，这个节点也就永远不会被启动。
			::std::terminate();
		}
		else
		{
			deliver_entries<Dependency>(::std::make_index_sequence<delivery_size<Dependency>>{}, ::std::forward<Receiver>(receiver));
		}
	}

	template<class Dependency, size_t... E, class Receiver>
	void deliver_entries(::std::index_sequence<E...>, Receiver&& receiver) noexcept
	{
		::stdexec::set_value(::std::forward<Receiver>(receiver), access<delivery_table<Dependency>[E]>()...);
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
			return ::stdexec::__throw_dependent_sender_error<Self>();
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
	using dependency = node_dependencies_t<Node>;
	using payload_type = decltype(::sdag::payload(::std::forward<Node>(node)));
	static_assert(node_payload<payload_type, input_sender<dependency>>,
		"sdag: 节点载荷既不是 sender，也不是能接受输入 sender 的适配器（例如 ::stdexec::then(f)）");
	if constexpr (sender_payload<payload_type>)
	{
		static_assert(input_count_v<::std::decay_t<payload_type>> == 0, "sdag: 只有控制依赖的节点 sender 里不能出现输入 sender");
		return ::std::decay_t<payload_type>(::sdag::payload(::std::forward<Node>(node)));
	}
	else
	{
		using child = decltype(::sdag::payload(::std::forward<Node>(node))(input_sender<dependency>{}));
		static_assert(input_count_v<child> == 1,
			"sdag: 节点适配器必须把输入 sender 恰好用一次，而且中间只能隔着 stdexec 的算法层："
			"它的值只会被交付一次，repeat / retry 这类会重启子 sender 的适配器不能套在输入上；"
			"不透明的自定义 sender 包着输入时这里会数成 0 次");
		return ::sdag::payload(::std::forward<Node>(node))(input_sender<dependency>{});
	}
}

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

/// 探针：when_all<汇点 sender（输入叶子换成占位 sender）...>。保留汇点的属性（完成调度器等），
/// 签名与真实降解后的汇点一致，又不需要整图环境就能被 domain 的 transform_sender 检查。
/// 真实的 when_all 至少两个子 sender；只有一个汇点时把它重复一次，免得 domain 按子节点数约束时漏判。
template<class Self, class Env>
[[nodiscard]] consteval info when_all_probe()
{
	infos probes;
	template for (constexpr size_t sink : ::std::define_static_array(shape_of_v<Self>.sinks()))
	{
		using dependency = meta::at_t<sink, typename data_t<Self>::dependencies>;
		using placeholder = placeholder_sender<typename [: input_completions_type<Self, dependency, node_env_t<Self, Env>>() :]>;
		probes.push_back(substituted(children_of(^^Self)[sink], ^^placeholder));
	}
	if (probes.size() == 1)
	{
		probes.push_back(probes[0]);
	}
	infos arguments{^^::stdexec::when_all_t, ^^::stdexec::__};
	arguments.insert(arguments.end(), probes.begin(), probes.end());
	return ::std::meta::substitute(^^::stdexec::__sexpr, arguments);
}

/// transform_sender 分两个阶段：start_t 用环境的起始 domain，set_value_t 用 sender 自己报告的完成域。
template<class OpTag, class Self, class Env>
[[nodiscard]] consteval info phase_domain()
{
	if constexpr (::std::same_as<OpTag, ::stdexec::start_t>)
	{
		return ^^::stdexec::__detail::__starting_domain_t<Env>;
	}
	else
	{
		return ^^::stdexec::__detail::__completing_domain_t<void, Self, Env>;
	}
}

/// 当前阶段的 domain 会不会定制覆盖在汇点上的 when_all：会，整图就降解成标准算法交给它；
/// 不会（默认 domain、只定制 bulk 的线程池 domain 等）就保持融合。"定制"按 __default_domain_like 判断。
/// 节点签名不合法时直接返回 false，把诊断留给后面的 connect。
template<class OpTag, class Self, class Env>
[[nodiscard]] consteval bool lowering_wanted_in() noexcept
{
	if constexpr (!analysis<Self, probe_env<Self, Env>>::result.valid)
	{
		return false;
	}
	else if constexpr (!analysis<Self, node_env_t<Self, Env>>::result.valid)
	{
		return false;
	}
	else
	{
		using domain = typename [: graph_detail::phase_domain<OpTag, Self, Env>() :];
		if constexpr (::std::same_as<domain, ::stdexec::default_domain>)
		{
			return false;
		}
		else
		{
			using probe = typename [: graph_detail::when_all_probe<Self, Env>() :];
			return !::stdexec::__default_domain_like<domain, OpTag, probe, Env>;
		}
	}
}

template<class OpTag, class Self, class Env>
inline constexpr bool lowering_wanted = graph_detail::lowering_wanted_in<OpTag, Self, Env>();

// ---------------------------------------------------------------- 属性（与 when_all 的 __attrs 同一套语义）

/// 一组节点在某环境下 set_value 完成域的公共域；有节点给不出（例如没有 set_value 完成）时为 void，
/// 让下面的查询在立即上下文里被排除掉（与 when_all 一致）。
[[nodiscard]] consteval info common_domain(infos const& nodes, info env)
{
	infos domains;
	for (info node : nodes)
	{
		if (!::std::meta::can_substitute(^^::stdexec::__completion_domain_of_t, {^^::stdexec::set_value_t, node, env}))
		{
			return ^^void;
		}
		domains.push_back(::std::meta::dealias(::std::meta::substitute(^^::stdexec::__completion_domain_of_t, {^^::stdexec::set_value_t, node, env})));
	}
	return ::std::meta::dealias(::std::meta::substitute(^^::stdexec::__common_domain_t, domains));
}

/// 完成域：值完成发生在最后一个汇点完成的地方——取汇点在整图节点环境下的公共完成域；
/// 错误 / 停止可能来自任一节点——取全部节点的公共域。完成行为取全部节点的合成。
template<class Data, class... Children>
struct attrs
{
	using self_type = ::stdexec::__sexpr<dag_t, Data, Children...>&&;

	template<class Env>
	[[nodiscard]] static consteval bool evaluable_in() noexcept
	{
		if constexpr (!analysis<self_type, probe_env<self_type, Env>>::result.valid)
		{
			return false;
		}
		else
		{
			return analysis<self_type, node_env_t<self_type, Env>>::result.valid;
		}
	}

	template<class Env>
	[[nodiscard]] static consteval info value_domain() noexcept
	{
		infos sinks;
		for (size_t sink : Data::shape.sinks())
		{
			sinks.push_back(children_of(^^self_type)[sink]);
		}
		return common_domain(sinks, ^^node_env_t<self_type, Env>);
	}

	template<class Env>
	[[nodiscard]] static consteval info any_node_domain() noexcept
	{
		return common_domain(children_of(^^self_type), ^^node_env_t<self_type, Env>);
	}

	template<class Env>
		requires (evaluable_in<Env>()) && (value_domain<Env>() != ^^void)
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_value_t>, Env const&) const noexcept
		-> typename [: value_domain<Env>() :];

	template<class Env>
		requires (evaluable_in<Env>()) && (value_domain<Env>() != ^^void) && (any_node_domain<Env>() != ^^void)
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_error_t>, Env const&) const noexcept
		-> ::stdexec::__common_domain_t<typename [: value_domain<Env>() :], typename [: any_node_domain<Env>() :]>;

	template<class Env>
		requires (evaluable_in<Env>()) && (value_domain<Env>() != ^^void) && (any_node_domain<Env>() != ^^void)
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_stopped_t>, Env const&) const noexcept
		-> ::stdexec::__common_domain_t<typename [: value_domain<Env>() :], typename [: any_node_domain<Env>() :]>;

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

/// 图 sender 的形状（邻接矩阵、入出度、拓扑序）。
template<class Graph>
constexpr auto shape_v = graph_detail::shape_of_v<Graph&&>;

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
