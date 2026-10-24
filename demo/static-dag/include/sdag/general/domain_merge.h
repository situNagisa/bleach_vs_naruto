#pragma once

/// domain 合并：一组 domain 合成哪一个。依赖：类型列表。
///
///   common_domain_t<type_list<Domain...>>
///       规则与 stdexec 相同：都相同就是它；否则取 common_type；再否则带上 default_domain 取 common_type；
///       都不行就是 indeterminate_domain（其中已有 indeterminate_domain<> 时就是它）
///
/// 两种实现用 SDAG_REFLECTION 隔开。

#include <type_traits>

#include <stdexec/execution.hpp>

#include "../config.h"
#include "type_list.h"

namespace sdag::general
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace refl
{

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

}

template<class Domains>
using common_domain_t = typename [: refl::common_domain(refl::arguments(^^Domains)) :];

#else

namespace detail
{

template<class Domains>
struct common_domain;

template<class... Domains>
struct common_domain<general::type_list<Domains...>>
{
	static consteval auto compute() noexcept
	{
		if constexpr (sizeof...(Domains) == 1)
		{
			return ::std::type_identity<Domains...[0]>{};
		}
		else if constexpr (requires { typename ::std::common_type_t<Domains...>; })
		{
			return ::std::type_identity<::std::common_type_t<Domains...>>{};
		}
		else if constexpr (requires { typename ::std::common_type_t<::stdexec::default_domain, Domains...>; })
		{
			return ::std::type_identity<::std::common_type_t<::stdexec::default_domain, Domains...>>{};
		}
		else if constexpr ((::std::is_same_v<Domains, ::stdexec::indeterminate_domain<>> || ...))
		{
			// 已经有一个"无法确定"的 domain，结果也就无法确定。
			return ::std::type_identity<::stdexec::indeterminate_domain<>>{};
		}
		else
		{
			return ::std::type_identity<::stdexec::indeterminate_domain<Domains...>>{};
		}
	}

	using type = typename decltype(compute())::type;
};

}

template<class Domains>
using common_domain_t = typename detail::common_domain<unique_t<Domains>>::type;

#endif

}

}
