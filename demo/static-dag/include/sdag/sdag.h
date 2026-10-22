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
/// 图本身是一个以 dag_t 为标签、可按 [tag, data, child...] 结构化绑定的 sender，子 sender 就是各节点。
/// 默认连接走 0 开销的融合 op-state；所在 domain 定制了 when_all 时自动降解为 when_all / split / let_value
/// 组合交给 domain；composed<FanIn, FanOut> 策略则无条件降解，作为语义参照与可配置入口。
///
/// 头文件按依赖从底到顶：
///   config.h          SDAG_REFLECTION、no_unique_address、ABI 命名空间等宏
///   meta.h            type_list、indexed_tuple 等与计算方式无关的基础设施
///   stdexec_bridge.h  对 stdexec 内部实现的全部依赖
///   fwd.h             打断头文件环的前向声明
///   node.h            节点：after<>、node<>、make_node、payload CPO 与 concept
///   compute/          编译期计算层：反射与模板两份实现，接口相同
///   shape.h           图形状：邻接矩阵、入出度、拓扑序、错误诊断
///   graph_traits.h    图的类型级查询：第 I 个节点是谁、依赖谁、谁读它的值
///   graph_sender.h    图 sender 的类型与 tuple-like 协议
///   node_input.h      节点怎么拿到前驱的值：输入 sender、节点环境、整图状态
///   attributes.h      图 sender 的完成签名、属性、降解判据
///   fused.h           融合 op-state（默认连接）
///   lowering.h        降解成 when_all / split / let_value
///   dag.h             dag_t、fused / composed 策略、dag()

#include "config.h"
#include "meta.h"
#include "stdexec_bridge.h"
#include "fwd.h"
#include "node.h"
#include "compute/primitives.h"
#include "shape.h"
#include "graph_traits.h"
#include "graph_sender.h"
#include "compute/analysis.h"
#include "node_input.h"
#include "attributes.h"
#include "fused.h"
#include "lowering.h"
#include "dag.h"
