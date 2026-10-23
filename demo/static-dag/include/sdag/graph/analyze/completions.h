#pragma once

/// 由"值类型列表、错误类型列表、是否停止"拼出 ::stdexec::completion_signatures。整图分析与输入签名用它，不对外导出。
///
///   detail::make_completions_t<HasValues, Values, Errors, SendsStopped>
///       HasValues 为假时没有 set_value 签名（某个节点永远不会带值结束）
///   detail::value_completions_t<Values>
///       只有一个 set_value(Values...) 签名（输入 sender 的签名）

#include <type_traits>
#include <vector>

#include <stdexec/execution.hpp>

#include "../../config.h"
#include "../../meta_program/type_list.h"
#include "../../meta_program/utility.h"
#include "../../meta_program/indexed_tuple.h"
#include "../../meta_program/reflection.h"

namespace sdag::analyze
{

inline namespace SDAG_ABI_NAMESPACE
{

namespace detail
{

#if SDAG_REFLECTION

namespace completions_refl
{

using namespace ::sdag::meta::refl;

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
using make_completions_t = typename [: completions_refl::make_completions(HasValues, ^^Values, ^^Errors, SendsStopped) :];

template<class Values>
using value_completions_t = typename [: completions_refl::value_completions(^^Values) :];

#else

template<bool HasValues, class Values, class Errors, bool SendsStopped>
struct make_completions;

template<class... Vs, class... Es, bool SendsStopped>
struct make_completions<true, meta::type_list<Vs...>, meta::type_list<Es...>, SendsStopped>
{
	using type = ::std::conditional_t<SendsStopped,
		::stdexec::completion_signatures<::stdexec::set_value_t(Vs...), ::stdexec::set_error_t(Es)..., ::stdexec::set_stopped_t()>,
		::stdexec::completion_signatures<::stdexec::set_value_t(Vs...), ::stdexec::set_error_t(Es)...>>;
};

template<class... Vs, class... Es, bool SendsStopped>
struct make_completions<false, meta::type_list<Vs...>, meta::type_list<Es...>, SendsStopped>
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
struct value_completions<meta::type_list<Vs...>>
{
	using type = ::stdexec::completion_signatures<::stdexec::set_value_t(Vs...)>;
};

template<class Values>
using value_completions_t = typename value_completions<Values>::type;

#endif

}

}

}
