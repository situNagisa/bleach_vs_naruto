#pragma once

/// 编译期计算层（一）的反射实现。接口说明见 compute/primitives.h。
///
/// 写法：类型用 std::meta::info 表示，类型列表就是 std::vector<info>，运算都是普通的 consteval 函数（refl::）；
/// 对外接口（文件末尾）把结果用 [: :] 拼回类型。GCC 16 的几个限制在用到的地方注明。

#include <array>
#include <cstddef>
#include <exception>
#include <meta>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include "../../meta.h"
#include "../../fwd.h"
#include "../../stdexec_bridge.h"

namespace sdag::compute
{

inline namespace SDAG_ABI_NAMESPACE
{

namespace refl
{

using info = ::std::meta::info;
using infos = ::std::vector<info>;

// ---------------------------------------------------------------- 类型列表

/// 模板特化的实参（先去掉别名）。
[[nodiscard]] consteval infos arguments(info type)
{
	return ::std::meta::template_arguments_of(::std::meta::dealias(type));
}

[[nodiscard]] consteval info list(infos const& types)
{
	return ::std::meta::substitute(^^meta::type_list, types);
}

[[nodiscard]] consteval bool same(info left, info right)
{
	return ::std::meta::dealias(left) == ::std::meta::dealias(right);
}

/// 不存在时等于 size。
[[nodiscard]] consteval ::std::size_t index_of(info type, infos const& types)
{
	for (::std::size_t index = 0; index < types.size(); ++index)
	{
		if (same(type, types[index]))
		{
			return index;
		}
	}
	return types.size();
}

/// 保留首次出现的顺序去重。
[[nodiscard]] consteval infos unique(infos const& types)
{
	infos result;
	for (info type : types)
	{
		if (index_of(type, result) == result.size())
		{
			result.push_back(type);
		}
	}
	return result;
}

[[nodiscard]] consteval infos concat(::std::vector<infos> const& lists)
{
	infos result;
	for (infos const& one : lists)
	{
		result.insert(result.end(), one.begin(), one.end());
	}
	return result;
}

/// 对每个类型套一层单参数模板（别名模板也可以）。
[[nodiscard]] consteval infos map(info function, infos const& types)
{
	infos result;
	for (info type : types)
	{
		result.push_back(::std::meta::dealias(::std::meta::substitute(function, {type})));
	}
	return result;
}

/// std::index_sequence<values...>
[[nodiscard]] consteval info indices(::std::vector<::std::size_t> const& values)
{
	infos constants;
	for (::std::size_t value : values)
	{
		constants.push_back(::std::meta::reflect_constant(value));
	}
	return ::std::meta::substitute(^^::std::index_sequence, constants);
}

/// 把一组键换成它们在 keys 里的索引（不存在的键给出 keys.size()）。
[[nodiscard]] consteval ::std::vector<::std::size_t> indices_of(infos const& wanted, infos const& keys)
{
	::std::vector<::std::size_t> result;
	for (info key : wanted)
	{
		result.push_back(index_of(key, keys));
	}
	return result;
}

/// 别名模板里不能直接写 {arguments(^^Lists)...} 这样的临时 vector（consteval-only 的值不能出现在常量求值之外），
/// 所以包一层 consteval 函数。
template<class... Lists>
[[nodiscard]] consteval info concat_lists()
{
	return list(concat({arguments(^^Lists)...}));
}

// ---------------------------------------------------------------- 完成签名

[[nodiscard]] consteval bool is_completion_signatures(info type)
{
	type = ::std::meta::dealias(type);
	return ::std::meta::has_template_arguments(type) && ::std::meta::template_of(type) == ^^::stdexec::completion_signatures;
}

template<class Signature>
struct signature_arguments;

template<class R, class... Args>
struct signature_arguments<R(Args...)>
{
	using type = meta::type_list<Args...>;
};

template<class Signature>
using signature_arguments_t = typename signature_arguments<Signature>::type;

/// 完成签名 Tag(Args...) 的参数类型（保留原始 cv/ref）。
[[nodiscard]] consteval infos arguments_of(info signature)
{
	return arguments(::std::meta::substitute(^^signature_arguments_t, {signature}));
}

/// 每个 set_value 签名的参数列表（type_list）。
[[nodiscard]] consteval infos value_lists(info completions)
{
	infos result;
	for (info signature : arguments(completions))
	{
		if (::std::meta::return_type_of(signature) == ^^::stdexec::set_value_t)
		{
			result.push_back(list(arguments_of(signature)));
		}
	}
	return result;
}

/// 去重后的原始错误类型。
[[nodiscard]] consteval infos error_types(info completions)
{
	infos result;
	for (info signature : arguments(completions))
	{
		if (::std::meta::return_type_of(signature) == ^^::stdexec::set_error_t)
		{
			result.push_back(arguments_of(signature)[0]);
		}
	}
	return unique(result);
}

[[nodiscard]] consteval bool sends_stopped(info completions)
{
	for (info signature : arguments(completions))
	{
		if (::std::meta::return_type_of(signature) == ^^::stdexec::set_stopped_t)
		{
			return true;
		}
	}
	return false;
}

[[nodiscard]] consteval bool all_nothrow_decay_copyable(infos const& types)
{
	for (info type : types)
	{
		if (!::std::meta::is_nothrow_constructible_type(::std::meta::decay(type), {type}))
		{
			return false;
		}
	}
	return true;
}

template<class R, class... Args>
using function_type = R(Args...);

[[nodiscard]] consteval info make_signature(info tag, infos const& arguments)
{
	infos all{tag};
	all.insert(all.end(), arguments.begin(), arguments.end());
	return ::std::meta::substitute(^^function_type, all);
}

[[nodiscard]] consteval info make_completions(bool has_values, info value_list, info error_list, bool stopped)
{
	infos signatures;
	if (has_values)
	{
		signatures.push_back(make_signature(^^::stdexec::set_value_t, arguments(value_list)));
	}
	for (info error : arguments(error_list))
	{
		signatures.push_back(make_signature(^^::stdexec::set_error_t, {error}));
	}
	if (stopped)
	{
		signatures.push_back(make_signature(^^::stdexec::set_stopped_t, {}));
	}
	return ::std::meta::substitute(^^::stdexec::completion_signatures, signatures);
}

// ---------------------------------------------------------------- sender 表达式

/// 所有对 sender 类型的检查都先做这一步：去别名、去 cv / 引用。
[[nodiscard]] consteval info plain(info type)
{
	return ::std::meta::remove_cvref(::std::meta::dealias(type));
}

[[nodiscard]] consteval bool specializes(info type, info primary)
{
	type = plain(type);
	return ::std::meta::has_template_arguments(type) && ::std::meta::template_of(type) == primary;
}

template<class Sender>
inline constexpr bool is_expression_v = bridge::sender_expression<Sender>;

/// stdexec 算法 sender 的子 sender（经 bridge::parts_t 读出）；不是 stdexec 算法 sender 的类型给出空列表。
[[nodiscard]] consteval infos children(info sender)
{
	sender = plain(sender);
	if (!::std::meta::extract<bool>(::std::meta::substitute(^^is_expression_v, {sender})))
	{
		return {};
	}
	infos const parts = arguments(::std::meta::substitute(^^bridge::parts_t, {sender}));
	return infos(parts.begin() + 2, parts.end());
}

/// 输入叶子的个数：递归穿过 stdexec 的算法层。
/// 嵌套的图是另一张图的节点，它里面的输入叶子属于内层图，不算；自定义 sender 拆不开，当作不透明的叶子。
[[nodiscard]] consteval ::std::size_t input_count(info sender)
{
	if (specializes(sender, ^^input_sender))
	{
		return 1;
	}
	if (specializes(sender, ^^graph_sender))
	{
		return 0;
	}
	::std::size_t total = 0;
	for (info child : children(sender))
	{
		total += input_count(child);
	}
	return total;
}

/// 输入叶子换成 input 之后的类型：沿 stdexec 的算法层重建，标签、data 不变，含输入叶子的子树递归替换，其余子树原样保留。
/// 重建出的是裸的 bridge::expression_t<Tag, Data, Child...>：stdexec 的算法 sender 都是它的空派生类，行为一致。
/// input_count 只穿过算法层去数叶子，所以这里遇到的含叶子的 sender 一定拆得开。
[[nodiscard]] consteval info substituted(info sender, info input)
{
	sender = plain(sender);
	if (specializes(sender, ^^input_sender))
	{
		return ::std::meta::decay(input);
	}
	if (input_count(sender) == 0)
	{
		return sender;
	}
	infos const parts = arguments(::std::meta::substitute(^^bridge::parts_t, {sender}));
	infos rebuilt{parts[0], parts[1]};
	for (::std::size_t index = 2; index < parts.size(); ++index)
	{
		rebuilt.push_back(substituted(parts[index], input));
	}
	return ::std::meta::substitute(^^bridge::expression_t, rebuilt);
}

}

// ---------------------------------------------------------------- 对外接口（名字与 templates/primitives.h 一一对应）

template<class T, class List>
inline constexpr ::std::size_t index_of_v = refl::index_of(^^T, refl::arguments(^^List));

template<class... Lists>
using concat_t = typename [: refl::concat_lists<Lists...>() :];

template<class List>
using unique_t = typename [: refl::list(refl::unique(refl::arguments(^^List))) :];

template<template<class> class Fn, class List>
using transform_t = typename [: refl::list(refl::map(^^Fn, refl::arguments(^^List))) :];

template<template<class...> class To, class List>
using rebind_t = typename [: ::std::meta::substitute(^^To, refl::arguments(^^List)) :];

template<auto Array>
using sequence_t = typename [: refl::indices(::std::vector<::std::size_t>(Array.begin(), Array.end())) :];

template<auto Array, class Fn>
constexpr void for_each_value(Fn&& fn)
{
	template for (constexpr auto value : Array)
	{
		fn.template operator()<value>();
	}
}

template<class Keys, class Dependency>
inline constexpr auto dependency_indices_v = meta::to_array<refl::arguments(^^Dependency).size()>(
	refl::indices_of(refl::arguments(^^Dependency), refl::arguments(^^Keys)));

template<class T>
inline constexpr bool is_completion_signatures_v = refl::is_completion_signatures(^^T);

/// 一组完成签名的摘要。每个节点至多一种 set_value 完成（when_all 的同一要求）。
template<class Completions>
struct completion_summary
{
	static constexpr ::std::size_t value_count = refl::value_lists(^^Completions).size();
	static_assert(value_count <= 1, "sdag: 每个节点至多只能有一种 set_value 完成；有多种时请先 ::stdexec::into_variant");

	/// 没有值完成的节点（例如 just_stopped()）永远不会带着值结束，它的后继也永远不会以值启动。
	static constexpr bool has_values = value_count == 1;
	using raw_values = typename [: has_values ? refl::value_lists(^^Completions)[0] : refl::list({}) :];
	using values = typename [: refl::list(refl::map(^^::std::decay_t, refl::arguments(^^raw_values))) :];
	using raw_errors = typename [: refl::list(refl::error_types(^^Completions)) :];
	using errors = typename [: refl::list(refl::map(^^::std::decay_t, refl::error_types(^^Completions))) :];
	static constexpr bool sends_stopped = refl::sends_stopped(^^Completions);
	/// 存进 op-state 的拷贝 / 移动是否不抛。
	static constexpr bool nothrow_values = refl::all_nothrow_decay_copyable(refl::arguments(^^raw_values));
	static constexpr bool nothrow_errors = refl::all_nothrow_decay_copyable(refl::error_types(^^Completions));
};

template<bool HasValues, class Values, class Errors, bool SendsStopped>
using make_completions_t = typename [: refl::make_completions(HasValues, ^^Values, ^^Errors, SendsStopped) :];

template<class Values>
using value_completions_t = typename [: ::std::meta::substitute(^^::stdexec::completion_signatures,
	{refl::make_signature(^^::stdexec::set_value_t, refl::arguments(^^Values))}) :];

template<class Sender>
inline constexpr ::std::size_t input_count_v = refl::input_count(^^Sender);

template<class Sender, class Input>
using substituted_t = typename [: refl::substituted(^^Sender, ^^Input) :];

}

}
