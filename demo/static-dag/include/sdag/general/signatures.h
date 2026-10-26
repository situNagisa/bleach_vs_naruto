#pragma once

/// 完成签名：拆开 / 拼装一组 stdexec 完成签名。
///
///   is_stdexec_completion_signatures_v<T>   T 是不是 ::stdexec::completion_signatures<...>
///                                           （get_completion_signatures 算不出签名时返回的是错误类型，sdag 据此判断节点是否合法）
///   completion_trait<Completions>           一组完成签名的摘要：值、错误、是否停止、存值是否会抛
///   make_completions_t<HasValues, Values, Errors, SendsStopped>
///                                           由值类型列表、错误类型列表、是否停止拼出 completion_signatures；HasValues 为假时没有 set_value
///
/// 两种实现用 SDAG_REFLECTION 隔开，对外名字相同。

#include <cstddef>
#include <type_traits>
#include <vector>

#include <stdexec/execution.hpp>

#include "../config.h"
#include "type_list.h"

namespace sdag::general
{

inline namespace SDAG_ABI_NAMESPACE
{

// ---------------------------------------------------------------- 拆开

#if SDAG_REFLECTION
namespace refl
{

[[nodiscard]] consteval bool is_completion_signatures(info type)
{
	return specializes(type, ^^::stdexec::completion_signatures);
}

template<class Signature>
struct signature_arguments;

template<class R, class... Args>
struct signature_arguments<R(Args...)>
{
	using type = general::type_list<Args...>;
};

template<class Signature>
using signature_arguments_t = typename signature_arguments<Signature>::type;

/// 完成签名 Tag(Args...) 的参数类型（保留原始 cv/ref）。
[[nodiscard]] consteval infos arguments_of(info signature)
{
	return arguments(::std::meta::substitute(^^signature_arguments_t, {signature}));
}

/// completions 里返回类型为 tag 的签名。
[[nodiscard]] consteval infos signatures_of(info completions, info tag)
{
	infos result;
	for (info signature : arguments(completions))
	{
		if (::std::meta::return_type_of(signature) == tag)
		{
			result.push_back(signature);
		}
	}
	return result;
}

/// 每个 set_value 签名的参数列表（type_list）。
[[nodiscard]] consteval infos value_lists(info completions)
{
	infos result;
	for (info signature : signatures_of(completions, ^^::stdexec::set_value_t))
	{
		result.push_back(list(arguments_of(signature)));
	}
	return result;
}

/// 去重后的原始错误类型。
[[nodiscard]] consteval infos error_types(info completions)
{
	infos result;
	for (info signature : signatures_of(completions, ^^::stdexec::set_error_t))
	{
		result.push_back(arguments_of(signature)[0]);
	}
	return unique(result);
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


}

template<class T>
inline constexpr bool is_stdexec_completion_signatures_v = refl::is_completion_signatures(^^T);

/// 一组完成签名的摘要。每个节点至多一种 set_value 完成（when_all 的同一要求）。
template<class Completions>
struct completion_trait
{
	static constexpr ::std::size_t value_count = refl::value_lists(^^Completions).size();
	static_assert(value_count <= 1, "sdag: 每个节点至多只能有一种 set_value 完成；有多种时请先 ::stdexec::into_variant");

	/// 没有值完成的节点（例如 just_stopped()）永远不会带着值结束，它的后继也永远不会以值启动。
	static constexpr bool has_values = value_count == 1;
	using raw_values = typename [: has_values ? refl::value_lists(^^Completions)[0] : refl::list({}) :];
	using values = typename [: refl::list(refl::map(^^::std::decay_t, refl::arguments(^^raw_values))) :];
	using raw_errors = typename [: refl::list(refl::error_types(^^Completions)) :];
	using errors = typename [: refl::list(refl::map(^^::std::decay_t, refl::arguments(^^raw_errors))) :];
	static constexpr bool sends_stopped = !refl::signatures_of(^^Completions, ^^::stdexec::set_stopped_t).empty();
	/// 存进 op-state 的拷贝 / 移动是否不抛。
	static constexpr bool nothrow_values = refl::all_nothrow_decay_copyable(refl::arguments(^^raw_values));
	static constexpr bool nothrow_errors = refl::all_nothrow_decay_copyable(refl::arguments(^^raw_errors));
};

#else
namespace detail
{

template<class Signature>
struct classify;

template<class... Args>
struct classify<::stdexec::set_value_t(Args...)>
{
	using values = general::type_list<general::type_list<Args...>>;
	using errors = general::type_list<>;
	static constexpr bool stopped = false;
};

template<class Error>
struct classify<::stdexec::set_error_t(Error)>
{
	using values = general::type_list<>;
	using errors = general::type_list<Error>;
	static constexpr bool stopped = false;
};

template<>
struct classify<::stdexec::set_stopped_t()>
{
	using values = general::type_list<>;
	using errors = general::type_list<>;
	static constexpr bool stopped = true;
};

template<class List>
inline constexpr bool all_nothrow_decay_copyable_v = false;

template<class... Ts>
inline constexpr bool all_nothrow_decay_copyable_v<general::type_list<Ts...>> = (::std::is_nothrow_constructible_v<::std::decay_t<Ts>, Ts> && ...);

}

template<class T>
inline constexpr bool is_stdexec_completion_signatures_v = false;

template<class... Signatures>
inline constexpr bool is_stdexec_completion_signatures_v<::stdexec::completion_signatures<Signatures...>> = true;

template<class Completions>
struct completion_trait;

/// 一组完成签名的摘要。每个节点至多一种 set_value 完成（when_all 的同一要求）。
template<class... Signatures>
struct completion_trait<::stdexec::completion_signatures<Signatures...>>
{
	using value_lists = general::concat_t<typename detail::classify<Signatures>::values...>;
	static constexpr ::std::size_t value_count = value_lists::size;
	static_assert(value_count <= 1, "sdag: 每个节点至多只能有一种 set_value 完成；有多种时请先 ::stdexec::into_variant");

	/// 没有值完成的节点（例如 just_stopped()）永远不会带着值结束，它的后继也永远不会以值启动。
	static constexpr bool has_values = value_count == 1;
	using raw_values = general::at_t<0, general::concat_t<value_lists, general::type_list<general::type_list<>>>>;
	using values = general::transform_t<::std::decay_t, raw_values>;
	using raw_errors = general::unique_t<general::concat_t<typename detail::classify<Signatures>::errors...>>;
	using errors = general::transform_t<::std::decay_t, raw_errors>;
	static constexpr bool sends_stopped = (detail::classify<Signatures>::stopped || ...);
	/// 存进 op-state 的拷贝 / 移动是否不抛。
	static constexpr bool nothrow_values = detail::all_nothrow_decay_copyable_v<raw_values>;
	static constexpr bool nothrow_errors = detail::all_nothrow_decay_copyable_v<raw_errors>;
};

#endif

// ---------------------------------------------------------------- 拼装

#if SDAG_REFLECTION

namespace refl
{

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

}

template<bool HasValues, class Values, class Errors, bool SendsStopped>
using make_completions_t = typename [: refl::make_completions(HasValues, ^^Values, ^^Errors, SendsStopped) :];

#else

namespace detail
{

template<bool When, class Signature>
using signature_if = ::std::conditional_t<When, general::type_list<Signature>, general::type_list<>>;

template<bool HasValues, class Values, class Errors, bool SendsStopped>
struct make_completions;

template<bool HasValues, class... Vs, class... Es, bool SendsStopped>
struct make_completions<HasValues, general::type_list<Vs...>, general::type_list<Es...>, SendsStopped>
{
	using type = general::rebind_t<::stdexec::completion_signatures, general::concat_t<
		signature_if<HasValues, ::stdexec::set_value_t(Vs...)>,
		general::type_list<::stdexec::set_error_t(Es)...>,
		signature_if<SendsStopped, ::stdexec::set_stopped_t()>>>;
};

}

template<bool HasValues, class Values, class Errors, bool SendsStopped>
using make_completions_t = typename detail::make_completions<HasValues, Values, Errors, SendsStopped>::type;

#endif

}

}
