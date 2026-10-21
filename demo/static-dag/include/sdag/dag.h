#pragma once

/// 入口：dag(policy, nodes...) / dag(nodes...)。

#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "node.h"
#include "fused.h"

namespace sdag
{

/// 策略：把一组节点组合成一个 sender。
template<class Policy, class... Nodes>
concept graph_policy_for = (graph_node<Nodes> && ...)
	&& requires(Policy const& policy, Nodes&&... nodes) {
		{ policy.compose(::std::forward<Nodes>(nodes)...) } -> ::stdexec::sender;
	};

template<class Policy, graph_node... Nodes>
	requires graph_policy_for<Policy, Nodes...>
[[nodiscard]] constexpr auto dag(Policy const& policy, Nodes&&... nodes)
	noexcept(noexcept(policy.compose(::std::forward<Nodes>(nodes)...)))
{
	return policy.compose(::std::forward<Nodes>(nodes)...);
}

template<graph_node... Nodes>
[[nodiscard]] constexpr auto dag(Nodes&&... nodes)
	noexcept(noexcept(fused::compose(::std::forward<Nodes>(nodes)...)))
{
	return fused::compose(::std::forward<Nodes>(nodes)...);
}

/// 纯类型层面的写法：static_assert(::stdexec::sender<dag_sender<fused, node<...>, ...>>)。
template<class Policy, class... Nodes>
	requires graph_policy_for<Policy, Nodes...>
using dag_sender = decltype(::std::declval<Policy const&>().compose(::std::declval<Nodes>()...));

}
