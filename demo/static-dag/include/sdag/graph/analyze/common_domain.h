#pragma once

/// 完成域的合并，供图 sender 的属性使用。
///
///   common_domain_t<type_list<Domain...>>
///       一组 domain 的公共 domain，规则与 stdexec 相同：都相同就是它；否则取 common_type；
///       再否则带上 default_domain 取 common_type；都不行就是 indeterminate_domain（其中已有 indeterminate_domain<> 时就是它）
///   common_completion_domain_t<Env, type_list<Node...>>
///       一组节点在 Env 下 set_value 完成域的公共域；有节点给不出（没有 set_value 完成、或者查不到完成域）时为 void

#include <type_traits>

#include <stdexec/execution.hpp>

#include "../../config.h"
#include "../../meta_program/type_list.h"
#include "../../meta_program/utility.h"
#include "../../meta_program/indexed_tuple.h"
#include "../../graph/traits.h"
#include "../../meta_program/reflection.h"
#include "../../meta_program/index_of.h"
#include "../../meta_program/concat.h"
#include "../../meta_program/unique.h"
#include "../../concurrency/completion_signatures.h"

namespace sdag::analyze
{

inline namespace SDAG_ABI_NAMESPACE
{

namespace detail
{

/// 节点在 Env 下的值完成域（公开接口 get_completion_domain<set_value_t>）。给不出时替换失败，供 SFINAE 判断。
template<class Node, class Env>
using node_domain_t = decltype(::stdexec::get_completion_domain<::stdexec::set_value_t>(
	::stdexec::get_env(::std::declval<Node const&>()), ::std::declval<Env const&>()));

}

#if SDAG_REFLECTION

namespace detail
{

using namespace ::sdag::meta::refl;

template<class Node, class Env>
inline constexpr bool sends_value_v = concurrency::completion_trait<::stdexec::completion_signatures_of_t<Node, Env>>::has_values;

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
			|| !::std::meta::can_substitute(^^detail::node_domain_t, {node, env}))
		{
			return ^^void;
		}
		domains.push_back(::std::meta::dealias(::std::meta::substitute(^^detail::node_domain_t, {node, env})));
	}
	return common_domain(domains);
}

}

template<class Domains>
using common_domain_t = typename [: detail::common_domain(meta::refl::arguments(^^Domains)) :];

template<class Env, class Nodes>
using common_completion_domain_t = typename [: detail::common_completion_domain(meta::refl::arguments(^^Nodes), ^^Env) :];

#else

namespace detail
{

template<class Domains>
struct common_domain;

template<class... Domains>
struct common_domain<meta::type_list<Domains...>>
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

template<class Node, class Env>
concept sends_value = requires { typename ::stdexec::completion_signatures_of_t<Node, Env>; }
	&& concurrency::completion_trait<::stdexec::completion_signatures_of_t<Node, Env>>::has_values
	&& requires { typename detail::node_domain_t<Node, Env>; };

template<class Env, class Nodes>
struct common_completion_domain
{
	using type = void;
};

template<class Env, class... Nodes>
	requires (sends_value<Nodes, Env> && ...)
struct common_completion_domain<Env, meta::type_list<Nodes...>>
{
	using type = typename common_domain<meta::unique_t<meta::type_list<detail::node_domain_t<Nodes, Env>...>>>::type;
};

}

template<class Domains>
using common_domain_t = typename detail::common_domain<meta::unique_t<Domains>>::type;

template<class Env, class Nodes>
using common_completion_domain_t = typename detail::common_completion_domain<Env, Nodes>::type;

#endif

}

}
