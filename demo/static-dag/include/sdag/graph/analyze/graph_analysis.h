#pragma once

/// analysis<Self, Env>：整图在节点环境 Env 下的分析结果。
///
///   valid                       所有节点在 Env 下都能给出完成签名
///   first_invalid_t             不合法时第一个不合法的结果（原样透传给 stdexec 诊断）；合法时为 void
///   slot_needed[J]              节点 J 的值要不要存进整图 op-state：既不是要存值的汇点、后继又都只是控制依赖，或者根本没有值时不存
///   direct_sink / sink_index    只有一个汇点时它的值不存槽位，完成时直接转发给整图的接收者
///   sends_stopped / has_errors / nothrow_store / can_fail / has_values
///   values_t<J>                 节点 J 衰变后的值类型（type_list）
///   error_types                 整图的错误类型（type_list）：各节点错误的并集，存值可能抛异常时再加 exception_ptr
///   completions                 整图的完成签名；不合法时为 void
///
/// 为什么按拓扑序逐个节点算：节点 N 是 then(input_sender, f) 这样的表达式，then 算签名时要问输入 sender，
/// 输入 sender 又要问前驱的签名——一条长链的签名计算会层层嵌套，几十个节点就撞上编译器的模板深度上限。
/// 先算前驱，后面的计算全部命中编译器已有的实例化结果，嵌套深度变成常数。
/// 反射实现用 template for 按拓扑序走；模板实现用 warm_up 基类链（基类先于派生类实例化）。
///
/// 反射实现的坑：类模板的成员别名在类实例化时就会求值，所以不合法时 completions 写成 valid ? ... : ^^void，
/// 不能去拼一个不合法的签名。模板实现在 valid_analysis 与 invalid_analysis 之间选，理由相同。

#include <array>
#include <cstddef>
#include <exception>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include "../../config.h"
#include "../../meta_program/type_list.h"
#include "../../meta_program/utility.h"
#include "../../meta_program/indexed_tuple.h"
#include "../../graph/traits.h"
#include "../../meta_program/reflection.h"
#include "../../meta_program/concat.h"
#include "../../meta_program/unique.h"
#include "../../meta_program/sequence.h"
#include "../../concurrency/completion_signatures.h"
#include "../../graph/analyze/completions.h"
#include "../../graph/analyze/consumers.h"

namespace sdag::analyze
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace detail
{

using namespace ::sdag::meta::refl;

/// libstdc++ 里 std::exception_ptr 是 using 声明，^^ 不能直接作用于它，经别名再 dealias。
using exception_pointer = ::std::exception_ptr;

template<::std::size_t N>
struct analysis_result
{
	bool valid = true;
	info first_invalid = ^^void;

	::std::array<bool, N> slot_needed{};
	info_array<N> values{};

	bool direct_sink = false;
	::std::size_t sink_index = 0;
	bool sends_stopped = false;
	bool has_errors = false;
	bool has_values = true;
	bool nothrow_store = true;
	bool can_fail = false;
	info error_types = ^^meta::type_list<>;
	info value_types = ^^meta::type_list<>;
};

template<class Self, class Env>
[[nodiscard]] consteval auto analyze()
{
	constexpr auto shape = graph_detail::shape_of_v<Self>;
	constexpr auto consumers = graph_detail::consumer_counts_v<Self>;
	analysis_result<shape.count> result;

	::std::array<bool, shape.count> has_values{};
	::std::array<bool, shape.count> nothrow_values{};
	::std::array<bool, shape.count> nothrow_errors{};
	info_array<shape.count> raw_values{};
	info_array<shape.count> errors{};

	// 范围必须是常量表达式：用静态成员，不用上面的局部变量。
	template for (constexpr ::std::size_t index : graph_detail::shape_of_v<Self>.order)
	{
		using completions = graph_detail::node_completions_t<Self, index, Env>;
		if constexpr (!concurrency::is_stdexec_completion_signatures_v<completions>)
		{
			if (result.valid)
			{
				result.valid = false;
				result.first_invalid = ::std::meta::dealias(^^completions);
			}
		}
		else
		{
			using trait = concurrency::completion_trait<completions>;
			has_values[index] = trait::has_values;
			raw_values[index] = ::std::meta::dealias(^^typename trait::raw_values);
			result.values[index] = ::std::meta::dealias(^^typename trait::values);
			errors[index] = ::std::meta::dealias(^^typename trait::errors);
			result.sends_stopped = result.sends_stopped || trait::sends_stopped;
			nothrow_values[index] = trait::nothrow_values;
			nothrow_errors[index] = trait::nothrow_errors;
		}
	}
	if (!result.valid)
	{
		return result;
	}

	result.direct_sink = shape.sink_count() == 1;
	result.sink_index = result.direct_sink ? shape.sinks()[0] : 0;
	for (::std::size_t index = 0; index < shape.count; ++index)
	{
		result.slot_needed[index] = arguments(result.values[index]).size() != 0
			&& ((shape.is_sink(index) && !result.direct_sink) || consumers[index] != 0);
		result.has_errors = result.has_errors || arguments(errors[index]).size() != 0;
		result.has_values = result.has_values && has_values[index];
		result.nothrow_store = result.nothrow_store && nothrow_errors[index] && (!result.slot_needed[index] || nothrow_values[index]);
	}
	result.can_fail = result.has_errors || result.sends_stopped || !result.nothrow_store;

	::std::vector<infos> error_lists;
	for (::std::size_t index = 0; index < shape.count; ++index)
	{
		error_lists.push_back(arguments(errors[index]));
	}
	if (!result.nothrow_store)
	{
		error_lists.push_back(infos{::std::meta::dealias(^^exception_pointer)});
	}
	result.error_types = list(unique(concat(error_lists)));

	if (result.direct_sink)
	{
		// 唯一的汇点直接转发，整图的值签名就是它的原始签名。
		result.value_types = raw_values[result.sink_index];
	}
	else
	{
		::std::vector<infos> value_lists;
		for (::std::size_t sink : shape.sinks())
		{
			value_lists.push_back(arguments(result.values[sink]));
		}
		result.value_types = list(concat(value_lists));
	}
	return result;
}

}

template<class Self, class Env>
struct analysis
{
	static constexpr auto result = detail::analyze<Self, Env>();

	static constexpr bool valid = result.valid;
	using first_invalid_t = typename [: result.first_invalid :];
	static constexpr auto slot_needed = result.slot_needed;
	static constexpr bool direct_sink = result.direct_sink;
	static constexpr ::std::size_t sink_index = result.sink_index;
	static constexpr bool sends_stopped = result.sends_stopped;
	static constexpr bool has_errors = result.has_errors;
	static constexpr bool nothrow_store = result.nothrow_store;
	static constexpr bool can_fail = result.can_fail;
	static constexpr bool has_values = result.has_values;

	template<::std::size_t J>
	using values_t = typename [: result.values[J] :];

	using error_types = typename [: result.error_types :];

	using completions = typename [: result.valid
		? detail::completions_refl::make_completions(result.has_values, result.value_types, result.error_types, result.sends_stopped)
		: ^^void :];
};

#else

namespace detail
{

using ::std::size_t;
using graph_detail::node_completions_t;

template<class Self, class Indices, class Env>
struct validity;

template<class Self, size_t... I, class Env>
struct validity<Self, ::std::index_sequence<I...>, Env>
{
	static constexpr bool value = (concurrency::is_stdexec_completion_signatures_v<node_completions_t<Self, I, Env>> && ...);

	/// 第一个不合法的结果（按 Indices 的顺序）。
	template<size_t Cursor = 0>
	static consteval auto first_invalid()
	{
		constexpr ::std::array<size_t, sizeof...(I)> indices{I...};
		if constexpr (Cursor == sizeof...(I))
		{
			return ::std::type_identity<void>{};
		}
		else if constexpr (!concurrency::is_stdexec_completion_signatures_v<node_completions_t<Self, indices[Cursor], Env>>)
		{
			return ::std::type_identity<node_completions_t<Self, indices[Cursor], Env>>{};
		}
		else
		{
			return first_invalid<Cursor + 1>();
		}
	}

	using first_invalid_t = typename decltype(first_invalid())::type;
};

/// 按拓扑序预热节点签名（见文件开头）。warm_up<N> 的基类是 warm_up<N - 1>，基类先实例化。
template<class Self, size_t Step, class Env>
struct warm_up : warm_up<Self, Step - 1, Env>
{
	static constexpr size_t index = graph_detail::shape_of_v<Self>.order[Step - 1];
	static constexpr bool done = warm_up<Self, Step - 1, Env>::done && sizeof(::std::type_identity<node_completions_t<Self, index, Env>>) > 0;
};

template<class Self, class Env>
struct warm_up<Self, 0, Env>
{
	static constexpr bool done = true;
};

template<class Self, class Env>
inline constexpr bool all_valid = warm_up<Self, graph_detail::count_of_v<Self>, Env>::done
	&& validity<Self, ::std::make_index_sequence<graph_detail::count_of_v<Self>>, Env>::value;

/// 唯一汇点时整图的值签名就是它的原始签名；延迟到确定 Direct 之后再取下标。
template<class Self, bool Direct, size_t Sink, class Fallback, class Env>
struct graph_value_types
{
	using type = Fallback;
};

template<class Self, size_t Sink, class Fallback, class Env>
struct graph_value_types<Self, true, Sink, Fallback, Env>
{
	using type = typename concurrency::completion_trait<node_completions_t<Self, Sink, Env>>::raw_values;
};

template<class Self, class Env, class Indices>
struct valid_analysis;

template<class Self, class Env, size_t... I>
struct valid_analysis<Self, Env, ::std::index_sequence<I...>>
{
	static constexpr auto shape = graph_detail::shape_of_v<Self>;
	static constexpr auto consumers = graph_detail::consumer_counts_v<Self>;

	template<size_t J>
	using node = concurrency::completion_trait<node_completions_t<Self, J, Env>>;

	static constexpr bool valid = true;
	using first_invalid_t = void;

	template<size_t J>
	using values_t = typename node<J>::values;

	static constexpr bool direct_sink = shape.sink_count() == 1;
	static constexpr size_t sink_index = direct_sink ? sinks_v<shape>[0] : size_t{0};

	static constexpr ::std::array<bool, sizeof...(I)> slot_needed{
		(values_t<I>::size != 0 && ((shape.is_sink(I) && !direct_sink) || consumers[I] != 0))...};

	static constexpr bool sends_stopped = (node<I>::sends_stopped || ...);
	static constexpr bool has_errors = ((node<I>::errors::size != 0) || ...);
	static constexpr bool nothrow_store = ((node<I>::nothrow_errors && (!slot_needed[I] || node<I>::nothrow_values)) && ...);
	static constexpr bool can_fail = has_errors || sends_stopped || !nothrow_store;
	static constexpr bool has_values = (node<I>::has_values && ...);

	using error_types = meta::unique_t<meta::concat_t<
		typename node<I>::errors...,
		::std::conditional_t<nothrow_store, meta::type_list<>, meta::type_list<::std::exception_ptr>>>>;
	using value_types = typename graph_value_types<Self, direct_sink, sink_index,
		meta::concat_t<::std::conditional_t<shape.is_sink(I), values_t<I>, meta::type_list<>>...>, Env>::type;
	using completions = make_completions_t<has_values, value_types, error_types, sends_stopped>;
};

template<class Self, class Env>
struct invalid_analysis
{
	static constexpr bool valid = false;
	using first_invalid_t = typename validity<Self, meta::sequence_t<graph_detail::shape_of_v<Self>.order>, Env>::first_invalid_t;
	static constexpr ::std::array<bool, graph_detail::count_of_v<Self>> slot_needed{};
	static constexpr bool direct_sink = false;
	static constexpr size_t sink_index = 0;
	static constexpr bool sends_stopped = false;
	static constexpr bool has_errors = false;
	static constexpr bool nothrow_store = true;
	static constexpr bool can_fail = false;
	static constexpr bool has_values = false;

	template<size_t J>
	using values_t = meta::type_list<>;

	using error_types = meta::type_list<>;
	using completions = void;
};

}

template<class Self, class Env>
struct analysis : ::std::conditional_t<detail::all_valid<Self, Env>,
	detail::valid_analysis<Self, Env, ::std::make_index_sequence<graph_detail::count_of_v<Self>>>,
	detail::invalid_analysis<Self, Env>>
{
};

#endif

}

}
