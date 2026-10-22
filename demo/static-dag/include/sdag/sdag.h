#pragma once

/// sdag：静态 DAG sender。
///
///   auto g = ::sdag::dag(
///       ::sdag::make_node<a>(::stdexec::just(1)),
///       ::sdag::make_node<b, ::sdag::after<a>>(::stdexec::then([](int const& x) { return x + 1; })),
///       ::sdag::make_node<c, ::sdag::after<a>>(::stdexec::then([](int const& x) { return x * 2; })),
///       ::sdag::make_node<d, ::sdag::after<b, c>>(::stdexec::then([](int y, int z) { return y + z; })));
///   static_assert(::stdexec::sender<decltype(g)>);
///   auto [sum] = ::stdexec::sync_wait(::std::move(g)).value();   // 4
///
/// 图本身是一个以 dag_t 为标签的 stdexec sender 表达式（__sexpr），子 sender 就是各节点。
/// 默认连接走 0 开销的融合 op-state；所在 domain 定制了 when_all 时自动降解为 when_all / split / let_value
/// 组合交给 domain；composed<FanIn, FanOut> 策略则无条件降解，作为语义参照与可配置入口。

#include "meta.h"
#include "signatures.h"
#include "node.h"
#include "shape.h"
#include "graph.h"
#include "fused.h"
#include "composed.h"
#include "dag.h"
