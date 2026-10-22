#pragma once

/// 完成签名代数：只认 ::stdexec::completion_signatures 这一种形状，用反射把它拆成
/// 值列表 / 错误列表 / 是否 stopped，再拼回去。

#include <exception>
#include <meta>
#include <type_traits>

#include <stdexec/execution.hpp>

#include "meta.h"

namespace sdag::sig
{

using meta::info;
using meta::infos;

/// libstdc++ 里 std::exception_ptr 是 using 声明，^^ 不能直接作用于它，经别名再 dealias。
using exception_pointer = ::std::exception_ptr;

[[nodiscard]] consteval info exception_ptr_type()
{
	return ::std::meta::dealias(^^exception_pointer);
}

[[nodiscard]] consteval bool is_completion_signatures(info type)
{
	type = ::std::meta::dealias(type);
	return ::std::meta::has_template_arguments(type) && ::std::meta::template_of(type) == ^^::stdexec::completion_signatures;
}

template<class T>
inline constexpr bool is_completion_signatures_v = sig::is_completion_signatures(^^T);

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
	return meta::arguments(::std::meta::substitute(^^signature_arguments_t, {signature}));
}

[[nodiscard]] consteval info tag_of(info signature)
{
	return ::std::meta::return_type_of(signature);
}

/// 一组完成签名的摘要。
struct summary
{
	/// 每个 set_value 签名对应一个 type_list<Args...>。
	infos value_lists;
	/// 去重后的原始错误类型。
	infos error_types;
	bool sends_stopped = false;
};

[[nodiscard]] consteval summary summarize(info completions)
{
	summary result;
	for (info signature : meta::arguments(completions))
	{
		info tag = tag_of(signature);
		if (tag == ^^::stdexec::set_value_t)
		{
			result.value_lists.push_back(meta::list(arguments_of(signature)));
		}
		else if (tag == ^^::stdexec::set_error_t)
		{
			result.error_types.push_back(arguments_of(signature)[0]);
		}
		else
		{
			result.sends_stopped = true;
		}
	}
	result.error_types = meta::unique(result.error_types);
	return result;
}

[[nodiscard]] consteval bool nothrow_decay_copyable(info type)
{
	return ::std::meta::is_nothrow_constructible_type(::std::meta::decay(type), {type});
}

[[nodiscard]] consteval bool all_nothrow_decay_copyable(infos const& types)
{
	for (info type : types)
	{
		if (!nothrow_decay_copyable(type))
		{
			return false;
		}
	}
	return true;
}

template<class T>
inline constexpr bool nothrow_decay_copyable_v = sig::nothrow_decay_copyable(^^T);

template<class R, class... Args>
using function_type = R(Args...);

[[nodiscard]] consteval info make_signature(info tag, infos const& arguments)
{
	infos all{tag};
	all.insert(all.end(), arguments.begin(), arguments.end());
	return ::std::meta::substitute(^^function_type, all);
}

/// 由"值签名的参数列表（has_values 为假时没有值完成）+ 错误类型列表 + 是否 stopped"拼出 completion_signatures。
[[nodiscard]] consteval info make_completions(bool has_values, info value_list, info error_list, bool sends_stopped)
{
	infos signatures;
	if (has_values)
	{
		signatures.push_back(make_signature(^^::stdexec::set_value_t, meta::arguments(value_list)));
	}
	for (info error : meta::arguments(error_list))
	{
		signatures.push_back(make_signature(^^::stdexec::set_error_t, {error}));
	}
	if (sends_stopped)
	{
		signatures.push_back(make_signature(^^::stdexec::set_stopped_t, {}));
	}
	return ::std::meta::substitute(^^::stdexec::completion_signatures, signatures);
}

}
