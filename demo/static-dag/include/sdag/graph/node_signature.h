#pragma once

/// 节点的签名：第 I 个节点在某个环境下的完成签名（只是对节点 sender 的一次查询）。依赖：图的描述。
///
///   node_completions_t<Self, I, Env...>   算不出来时是 stdexec 给出的错误类型（不是硬错误）

#include <cstddef>

#include <stdexec/execution.hpp>

#include "description.h"

namespace sdag::graph
{

template<class Self, size_t I, class... Env>
using node_completions_t = decltype(::stdexec::get_completion_signatures<node_sender_t<Self, I>, Env...>());

}
