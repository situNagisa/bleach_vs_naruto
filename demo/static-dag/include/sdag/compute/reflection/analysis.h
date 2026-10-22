#pragma once

/// 编译期计算层（二）的反射实现。接口说明见 compute/analysis.h。
///
/// 整图分析是一个按拓扑序展开的 consteval 函数 analyze()，结果是一个字面量结构体 analysis_result，
/// 对外的 analysis<Self, Env> 只是把它的字段转出来。
///
/// 反射特有的两个坑：
///   - 类模板的成员别名在类实例化时就会求值。节点签名不合法时 analysis::completions 不能去拼签名（会在不合法的结果上
///     抛 std::meta::exception），所以写成 valid ? make_completions(...) : ^^void。
///   - consteval 计算没有编译器缓存。降解的类型（lower_types）如果写成"节点类型依赖前驱类型"的互递归函数，
///     计算量随图里的路径数指数增长；所以按拓扑序一次填进表里，后面查表。模板版用类模板，编译器会缓存，没有这个问题。

#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <meta>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include "../../meta.h"
#include "../../graph_traits.h"
#include "primitives.h"

namespace sdag::compute
{

inline namespace SDAG_ABI_NAMESPACE
{

namespace refl
{

using graph_detail::traits_of;

/// libstdc++ 里 std::exception_ptr 是 using 声明，^^ 不能直接作用于它，经别名再 dealias。
using exception_pointer = ::std::exception_ptr;

/// libstdc++ 的 std::array<T, 0> 没有 consteval 的 operator[]，装不了 consteval-only 的 info；空图时留一个占位元素。
template<::std::size_t N>
using info_array = ::std::array<info, N == 0 ? 1 : N>;

template<::std::size_t N>
struct analysis_result
{
	bool valid = true;
	info first_invalid = ^^void;

	::std::array<bool, N> slot_needed{};
	/// 每个节点衰变后的值类型列表。
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

/// 按拓扑序逐个节点取完成签名并汇总。
///
/// 为什么按拓扑序：节点 N 是 then(input_sender, f) 这样的表达式，then 算签名时要问输入 sender，输入 sender 又要问前驱的签名——
/// 一条长链的签名计算会层层嵌套，几十个节点就撞上编译器的模板深度上限。先算前驱，后面的计算全部命中编译器已有的
/// 实例化结果，嵌套深度变成常数。
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
		if constexpr (!is_completion_signatures_v<completions>)
		{
			if (result.valid)
			{
				result.valid = false;
				result.first_invalid = ::std::meta::dealias(^^completions);
			}
		}
		else
		{
			using summary = completion_summary<completions>;
			has_values[index] = summary::has_values;
			raw_values[index] = ::std::meta::dealias(^^typename summary::raw_values);
			result.values[index] = ::std::meta::dealias(^^typename summary::values);
			errors[index] = ::std::meta::dealias(^^typename summary::errors);
			result.sends_stopped = result.sends_stopped || summary::sends_stopped;
			nothrow_values[index] = summary::nothrow_values;
			nothrow_errors[index] = summary::nothrow_errors;
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

/// 共享的值按 const& 交给后继；独占的值 move 过去（签名写成按值）。
[[nodiscard]] consteval info access_type(info value, bool shared)
{
	return shared ? ::std::meta::add_lvalue_reference(::std::meta::add_const(value)) : value;
}

/// 输入 sender 的完成签名。只看前驱节点的签名，不碰整图分析：
/// 整图分析要算每个节点的签名，节点的签名又要问它的输入 sender——如果输入 sender 反过来要整图分析，就成环了。
template<class Self, class Dependency, class Env>
[[nodiscard]] consteval info input_completions()
{
	infos values;
	bool has_values = true;
	template for (constexpr ::std::size_t from : graph_detail::dependency_indices_v<Self, Dependency>)
	{
		static_assert(from < graph_detail::count_of_v<Self>, "sdag: 输入 sender 引用了图里不存在的键");
		using completions = graph_detail::node_completions_t<Self, from, Env>;
		if constexpr (!is_completion_signatures_v<completions>)
		{
			return ::std::meta::dealias(^^completions);
		}
		else
		{
			using summary = completion_summary<completions>;
			if (!summary::has_values)
			{
				has_values = false;
			}
			for (info value : arguments(^^typename summary::values))
			{
				values.push_back(access_type(value, graph_detail::shared_v<Self, from>));
			}
		}
	}
	if (!has_values)
	{
		return ^^::stdexec::completion_signatures<>;
	}
	return ::std::meta::substitute(^^::stdexec::completion_signatures, {make_signature(^^::stdexec::set_value_t, values)});
}

/// 每个节点降解后的三种类型，按拓扑序一次算完存成表（为什么不写成互递归见文件开头）。
template<::std::size_t N>
struct lowered_tables
{
	info_array<N> input{};
	info_array<N> node_sender{};
	info_array<N> built{};
};

template<class Self, class FanIn, class FanOut>
[[nodiscard]] consteval auto lower_types()
{
	constexpr auto shape = graph_detail::shape_of_v<Self>;
	lowered_tables<shape.count> tables;
	infos const keys = arguments(^^typename traits_of<Self>::data::keys);
	infos const dependencies = arguments(^^typename traits_of<Self>::data::dependencies);
	infos const children = arguments(^^typename traits_of<Self>::children);

	// 后继取前驱的方式：共享的拷贝一份扇出 sender，独占的直接 move。
	auto dependency_input = [&](::std::size_t from)
	{
		return shape.out_degree[from] > 1
			? ::std::meta::add_lvalue_reference(::std::meta::add_const(tables.built[from]))
			: ::std::meta::add_rvalue_reference(tables.built[from]);
	};

	for (::std::size_t index : shape.order)
	{
		infos fanin_arguments{^^FanIn const&};
		for (::std::size_t from : indices_of(arguments(dependencies[index]), keys))
		{
			fanin_arguments.push_back(dependency_input(from));
		}
		tables.input[index] = fanin_arguments.size() == 1
			? ::std::meta::dealias(^^decltype(::stdexec::just()))
			: ::std::meta::dealias(::std::meta::substitute(^^::std::invoke_result_t, fanin_arguments));

		info const child = ::std::meta::substitute(^^meta::copy_cvref_t, {^^Self, children[index]});
		if (input_count(child) != 0)
		{
			tables.node_sender[index] = substituted(child, tables.input[index]);
		}
		else if (shape.is_root(index))
		{
			tables.node_sender[index] = plain(child);
		}
		else
		{
			tables.node_sender[index] = ::std::meta::dealias(::std::meta::substitute(^^::std::invoke_result_t,
				{^^::stdexec::let_value_t const&, tables.input[index], ::std::meta::substitute(^^graph_detail::constant_sender, {plain(child)})}));
		}

		tables.built[index] = shape.out_degree[index] > 1
			? ::std::meta::dealias(::std::meta::substitute(^^::std::invoke_result_t, {^^FanOut const&, tables.node_sender[index]}))
			: tables.node_sender[index];
	}
	return tables;
}

template<::std::size_t N>
[[nodiscard]] consteval info storage_type(lowered_tables<N> const& tables)
{
	infos slots;
	for (::std::size_t index = 0; index < N; ++index)
	{
		slots.push_back(::std::meta::substitute(^^::std::optional, {tables.built[index]}));
	}
	return ::std::meta::substitute(^^::std::tuple, slots);
}

template<class Node, class Env>
inline constexpr bool sends_value_v = completion_summary<::stdexec::completion_signatures_of_t<Node, Env>>::has_values;

/// 一组 domain 的公共 domain（与 stdexec 的规则一致）：都相同就是它；否则取 common_type；
/// 再否则带上 default_domain 取 common_type；都不行就是 indeterminate_domain（其中已有 indeterminate_domain<> 时就是它）。
[[nodiscard]] consteval info common_domain(infos domains)
{
	domains = unique(domains);
	if (domains.size() == 1)
	{
		return domains[0];
	}
	if (::std::meta::can_substitute(^^::std::common_type_t, domains))
	{
		return ::std::meta::dealias(::std::meta::substitute(^^::std::common_type_t, domains));
	}
	infos with_default = concat({{^^::stdexec::default_domain}, domains});
	if (::std::meta::can_substitute(^^::std::common_type_t, with_default))
	{
		return ::std::meta::dealias(::std::meta::substitute(^^::std::common_type_t, with_default));
	}
	// 已经有一个"无法确定"的 domain，结果也就无法确定。
	if (index_of(^^::stdexec::indeterminate_domain<>, domains) != domains.size())
	{
		return ^^::stdexec::indeterminate_domain<>;
	}
	return ::std::meta::substitute(^^::stdexec::indeterminate_domain, domains);
}

[[nodiscard]] consteval info common_completion_domain(infos const& nodes, info env)
{
	infos domains;
	for (info node : nodes)
	{
		if (!::std::meta::can_substitute(^^::stdexec::completion_signatures_of_t, {node, env})
			|| !::std::meta::extract<bool>(::std::meta::substitute(^^sends_value_v, {node, env}))
			|| !::std::meta::can_substitute(^^graph_detail::node_domain_t, {node, env}))
		{
			return ^^void;
		}
		domains.push_back(::std::meta::dealias(::std::meta::substitute(^^graph_detail::node_domain_t, {node, env})));
	}
	return common_domain(domains);
}

}

// ---------------------------------------------------------------- 对外接口（名字与 templates/analysis.h 一一对应）

template<class Self, class Env>
struct analysis
{
	static constexpr auto result = refl::analyze<Self, Env>();

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

	/// 类的成员别名在类实例化时就会求值，所以不合法时给 void，别让 make_completions 在不合法的结果上抛异常。
	using completions = typename [: result.valid
		? refl::make_completions(result.has_values, result.value_types, result.error_types, result.sends_stopped)
		: ^^void :];
};

template<class Self, class Dependency, class Env>
using input_completions_t = typename [: refl::input_completions<Self, Dependency, Env>() :];

template<class Self, class FanIn, class FanOut>
struct lowered
{
	static constexpr auto tables = refl::lower_types<Self, FanIn, FanOut>();

	template<::std::size_t I>
	using input_t = typename [: tables.input[I] :];

	template<::std::size_t I>
	using node_sender_t = typename [: tables.node_sender[I] :];

	template<::std::size_t I>
	using built_t = typename [: tables.built[I] :];

	using storage_t = typename [: refl::storage_type(tables) :];
};

template<class Domains>
using common_domain_t = typename [: refl::common_domain(refl::arguments(^^Domains)) :];

template<class Env, class Nodes>
using common_completion_domain_t = typename [: refl::common_completion_domain(refl::arguments(^^Nodes), ^^Env) :];

}

}
