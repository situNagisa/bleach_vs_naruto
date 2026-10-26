#pragma once

/// 节点的完成域：一个节点在某个环境下的值完成域，以及一组节点的公共值完成域。
///
///   node_domain_t<Node, Env>                 节点在 Env 下的值完成域（公开接口 get_completion_domain<set_value_t>）；给不出时替换失败
///   common_completion_domain_t<Env, type_list<Node...>>
///                                            一组节点在 Env 下值完成域的公共域；有节点给不出（没有值完成、或者查不到完成域）时为 void
///
/// 有反射与模板两种实现，用 SDAG_REFLECTION 隔开。

#include <type_traits>

#include <stdexec/execution.hpp>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/signatures.h"
#include "../general/domain_merge.h"
#include "description.h"

namespace sdag::graph
{

/// 节点在 Env 下的值完成域（公开接口 get_completion_domain<set_value_t>）。给不出时替换失败，供 SFINAE 判断。
template<class Node, class Env>
using node_domain_t = decltype(::stdexec::get_completion_domain<::stdexec::set_value_t>(
	::stdexec::get_env(::std::declval<Node const&>()), ::std::declval<Env const&>()));

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace detail
{

using namespace ::sdag::general::refl;

template<class Node, class Env>
inline constexpr bool sends_value_v = general::completion_trait<::stdexec::completion_signatures_of_t<Node, Env>>::has_values;

[[nodiscard]] consteval info common_completion_domain(infos const& nodes, info env)
{
	infos domains;
	for (info node : nodes)
	{
		if (!::std::meta::can_substitute(^^::stdexec::completion_signatures_of_t, {node, env})
			|| !::std::meta::extract<bool>(::std::meta::substitute(^^sends_value_v, {node, env}))
			|| !::std::meta::can_substitute(^^node_domain_t, {node, env}))
		{
			return ^^void;
		}
		domains.push_back(::std::meta::dealias(::std::meta::substitute(^^node_domain_t, {node, env})));
	}
	return general::refl::common_domain(domains);
}

}

template<class Env, class Nodes>
using common_completion_domain_t = typename [: detail::common_completion_domain(general::refl::arguments(^^Nodes), ^^Env) :];

#else

namespace detail
{

template<class Node, class Env>
concept sends_value = requires { typename ::stdexec::completion_signatures_of_t<Node, Env>; }
	&& general::completion_trait<::stdexec::completion_signatures_of_t<Node, Env>>::has_values
	&& requires { typename node_domain_t<Node, Env>; };

template<class Env, class Nodes>
struct common_completion_domain
{
	using type = void;
};

template<class Env, class... Nodes>
	requires (sends_value<Nodes, Env> && ...)
struct common_completion_domain<Env, general::type_list<Nodes...>>
{
	using type = general::common_domain_t<general::type_list<node_domain_t<Nodes, Env>...>>;
};

}

template<class Env, class Nodes>
using common_completion_domain_t = typename detail::common_completion_domain<Env, Nodes>::type;

#endif

}

}
