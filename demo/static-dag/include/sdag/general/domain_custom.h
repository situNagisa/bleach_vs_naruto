#pragma once

/// domain 改写：某个 domain 在某个阶段会不会改写某个 sender。依赖：无。
///
///   phase_domain<OpTag, Sender, Env>()     transform_sender 这一阶段问的是哪个 domain（返回 type_identity）
///   customizes<Domain, OpTag, Sender, Env>  Domain 会不会改写 Sender

#include <concepts>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

namespace sdag::general
{

/// transform_sender 这一阶段问的是哪个 domain。规则与 stdexec 相同：
/// start_t 阶段用 get_domain(env)；set_value_t 阶段用 get_completion_domain<>（先问 void 版本，再回落到 set_value_t）。
template<class OpTag, class Sender, class Env>
[[nodiscard]] consteval auto phase_domain() noexcept
{
	if constexpr (::std::same_as<OpTag, ::stdexec::start_t>)
	{
		return ::std::type_identity<decltype(::stdexec::get_domain(::std::declval<Env const&>()))>{};
	}
	else if constexpr (requires { ::stdexec::get_completion_domain<>(::stdexec::get_env(::std::declval<Sender>()), ::std::declval<Env const&>()); })
	{
		return ::std::type_identity<decltype(::stdexec::get_completion_domain<>(
			::stdexec::get_env(::std::declval<Sender>()), ::std::declval<Env const&>()))>{};
	}
	else
	{
		return ::std::type_identity<::stdexec::default_domain>{};
	}
}

/// Domain 会不会改写 Sender：它有能接受这个 sender 的 transform_sender，并且结果与 default_domain 的不同。
template<class Domain, class OpTag, class Sender, class Env>
concept customizes = requires { ::std::declval<Domain>().transform_sender(OpTag{}, ::std::declval<Sender>(), ::std::declval<Env const&>()); }
	&& !::std::same_as<
		decltype(::std::declval<Domain>().transform_sender(OpTag{}, ::std::declval<Sender>(), ::std::declval<Env const&>())),
		decltype(::stdexec::default_domain{}.transform_sender(OpTag{}, ::std::declval<Sender>(), ::std::declval<Env const&>()))>;

}
