#pragma once

/// 拼装一组完成签名。
///
///   make_completions_t<HasValues, Values, Errors, SendsStopped>
///       由值类型列表、错误类型列表、是否停止拼出 ::stdexec::completion_signatures；HasValues 为假时没有 set_value 签名
///   value_completions_t<Values>
///       只有一个 set_value(Values...) 签名
///
/// 两种实现用 SDAG_REFLECTION 隔开，对外名字相同。

#include <type_traits>
#include <vector>

#include <stdexec/execution.hpp>

#include "../../config.h"
#include "../type_list.h"

namespace sdag::general
{

inline namespace SDAG_ABI_NAMESPACE
{

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

[[nodiscard]] consteval info value_completions(info value_list)
{
	return ::std::meta::substitute(^^::stdexec::completion_signatures, {make_signature(^^::stdexec::set_value_t, arguments(value_list))});
}

}

template<bool HasValues, class Values, class Errors, bool SendsStopped>
using make_completions_t = typename [: refl::make_completions(HasValues, ^^Values, ^^Errors, SendsStopped) :];

template<class Values>
using value_completions_t = typename [: refl::value_completions(^^Values) :];

#else

template<bool HasValues, class Values, class Errors, bool SendsStopped>
struct make_completions;

template<class... Vs, class... Es, bool SendsStopped>
struct make_completions<true, general::type_list<Vs...>, general::type_list<Es...>, SendsStopped>
{
	using type = ::std::conditional_t<SendsStopped,
		::stdexec::completion_signatures<::stdexec::set_value_t(Vs...), ::stdexec::set_error_t(Es)..., ::stdexec::set_stopped_t()>,
		::stdexec::completion_signatures<::stdexec::set_value_t(Vs...), ::stdexec::set_error_t(Es)...>>;
};

template<class... Vs, class... Es, bool SendsStopped>
struct make_completions<false, general::type_list<Vs...>, general::type_list<Es...>, SendsStopped>
{
	using type = ::std::conditional_t<SendsStopped,
		::stdexec::completion_signatures<::stdexec::set_error_t(Es)..., ::stdexec::set_stopped_t()>,
		::stdexec::completion_signatures<::stdexec::set_error_t(Es)...>>;
};

template<bool HasValues, class Values, class Errors, bool SendsStopped>
using make_completions_t = typename make_completions<HasValues, Values, Errors, SendsStopped>::type;

template<class Values>
struct value_completions;

template<class... Vs>
struct value_completions<general::type_list<Vs...>>
{
	using type = ::stdexec::completion_signatures<::stdexec::set_value_t(Vs...)>;
};

template<class Values>
using value_completions_t = typename value_completions<Values>::type;

#endif

}

}
