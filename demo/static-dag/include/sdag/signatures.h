#pragma once

/// 完成签名代数：只认 ::stdexec::completion_signatures 这一种形状，把它拆成
/// 值列表 / 错误列表 / 是否 stopped，再拼回去。不依赖 stdexec 的内部元函数。

#include <type_traits>

#include <stdexec/execution.hpp>

#include "meta.h"

namespace sdag::sig
{

template<class T>
inline constexpr bool is_completion_signatures = false;

template<class... Sigs>
inline constexpr bool is_completion_signatures<::stdexec::completion_signatures<Sigs...>> = true;

template<class Sig>
struct classify;

template<class... Args>
struct classify<::stdexec::set_value_t(Args...)>
{
	using values = meta::type_list<meta::type_list<Args...>>;
	using errors = meta::type_list<>;
	static constexpr bool stopped = false;
};

template<class Error>
struct classify<::stdexec::set_error_t(Error)>
{
	using values = meta::type_list<>;
	using errors = meta::type_list<Error>;
	static constexpr bool stopped = false;
};

template<>
struct classify<::stdexec::set_stopped_t()>
{
	using values = meta::type_list<>;
	using errors = meta::type_list<>;
	static constexpr bool stopped = true;
};

template<class Completions>
struct summary;

template<class... Sigs>
struct summary<::stdexec::completion_signatures<Sigs...>>
{
	/// 每个 set_value 签名对应一个 type_list<Args...>（保留原始 cv/ref）。
	using value_lists = meta::concat_t<typename classify<Sigs>::values...>;
	/// 去重后的原始错误类型。
	using error_types = meta::unique_t<meta::concat_t<typename classify<Sigs>::errors...>>;
	static constexpr bool sends_stopped = (classify<Sigs>::stopped || ...);
};

template<class T>
using decay_t = ::std::decay_t<T>;

template<class T>
inline constexpr bool nothrow_decay_copyable = ::std::is_nothrow_constructible_v<::std::decay_t<T>, T>;

template<class List>
struct all_nothrow_decay_copyable;

template<class... Ts>
struct all_nothrow_decay_copyable<meta::type_list<Ts...>>
{
	static constexpr bool value = (nothrow_decay_copyable<Ts> && ...);
};

template<class ValueList>
struct value_signature;

template<class... Vs>
struct value_signature<meta::type_list<Vs...>>
{
	using type = ::stdexec::set_value_t(Vs...);
};

template<class ValueList>
using value_signature_t = typename value_signature<ValueList>::type;

template<class Error>
using error_signature_t = ::stdexec::set_error_t(Error);

/// 由"值签名的参数列表（HasValues 为假时没有值完成）+ 错误类型列表 + 是否 stopped"拼出 completion_signatures。
template<bool HasValues, class ValueList, class ErrorList, bool SendsStopped>
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

template<bool HasValues, class ValueList, class ErrorList, bool SendsStopped>
using make_completions_t = typename make_completions<HasValues, ValueList, ErrorList, SendsStopped>::type;

}
