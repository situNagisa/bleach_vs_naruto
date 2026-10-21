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
/// 默认策略 fused 是 0 开销的：整图一个 op-state，没有堆分配。
/// 组合策略 composed<FanIn, FanOut> 用 when_all / split 拼树，作为语义参照与可配置入口。

#include "meta.h"
#include "signatures.h"
#include "node.h"
#include "shape.h"
#include "fused.h"
#include "composed.h"
#include "dag.h"
