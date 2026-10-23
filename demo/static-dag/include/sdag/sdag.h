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
/// 模块（依赖从底到顶，下层不认识上层）：
///   config.h, fwd.h   编译开关与打断头文件环的前向声明
///   meta_program/     与 sdag 无关的元编程：type_list 与其上的元函数、indexed_tuple、小工具
///   concurrency/      与 stdexec 打交道的通用部分：完成签名的读取、stdexec 内部接口的桥、constant_sender
///   graph/            图本身：节点（node）、形状（shape）、类型级查询（traits）、编译期分析（analyze/）
///   execution/        图怎么作为 sender 执行：图 sender、输入 sender 与整图状态、属性、融合 op-state、降解
///   dag.h             入口：dag_t、fused / composed 策略、dag()
///
/// meta_program / concurrency / graph/analyze 下每个文件都有反射与模板两种实现，用 SDAG_REFLECTION 隔开。

#include "config.h"
#include "fwd.h"
#include "meta_program/type_list.h"
#include "meta_program/utility.h"
#include "meta_program/indexed_tuple.h"
#include "meta_program/index_of.h"
#include "meta_program/concat.h"
#include "meta_program/unique.h"
#include "meta_program/transform.h"
#include "meta_program/rebind.h"
#include "meta_program/dependency_indices.h"
#include "meta_program/sequence.h"
#include "meta_program/for_each_value.h"
#include "concurrency/stdexec_bridge.h"
#include "concurrency/completion_signatures.h"
#include "concurrency/constant_sender.h"
#include "graph/node.h"
#include "graph/shape.h"
#include "graph/traits.h"
#include "graph/analyze/sender_expression.h"
#include "graph/analyze/consumers.h"
#include "graph/analyze/graph_analysis.h"
#include "graph/analyze/input_completions.h"
#include "graph/analyze/lowered.h"
#include "graph/analyze/common_domain.h"
#include "execution/graph_sender.h"
#include "execution/node_input.h"
#include "execution/attributes.h"
#include "execution/fused.h"
#include "execution/lowering.h"
#include "dag.h"
