#pragma once

/// sdag：静态 DAG sender。
///
///   auto g = ::sdag::dag(
///       ::sdag::make_node<a>(::stdexec::just(1)),
///       ::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([](int const& x) { return x + 1; })),
///       ::sdag::make_node<c, ::sdag::data<a>>(::stdexec::then([](int const& x) { return x * 2; })),
///       ::sdag::make_node<d, ::sdag::data<b, c>>(::stdexec::then([](int y, int z) { return y + z; })));
///   static_assert(::stdexec::sender<decltype(g)>);
///   auto [sum] = ::stdexec::sync_wait(::std::move(g)).value();   // 4
///
/// 目录对应概念模型的分组，文件对应模块（依赖从上往下，下层不认识上层）：
///
///   general/     通用，与图无关
///     type_list.h            类型列表：一组类型怎么查、去重、拼接、映射
///     sequence.h             编译期序列：一串编译期整数怎么展开成逐项调用 / 参数包
///     tuple.h                不可移动元组：按序存放、就地构造不可移动对象、空元素 0 字节
///     digraph.h              有向无环图：拓扑序、入出度、根与汇点、成环检查
///     cvref.h                cv/引用转抄：让一个类型带上另一个类型的 const 与值类别
///     signatures.h           完成签名：怎么拆开 / 拼装一组完成签名
///     domain_merge.h         domain 合并：一组 domain 合成哪一个
///   stdexec/     对 stdexec 的适配
///     expressions.h          sender 表达式：拆开 / 重建 stdexec 的算法 sender
///     dependent.h            依赖型报错：「没有环境，算不出签名」怎么向 stdexec 报告
///   node/        节点
///     node.h                 节点：一个节点是什么：键 + 依赖声明 + 载荷
///     node_graph.h           节点集合成图：一组节点的键与依赖 → 一张有向无环图，以及键相关的错误诊断
///   protocol/    节点与图之间的约定
///     value_source.h         取值源协议：节点到哪里取前驱的值：环境里的句柄 + 两个接口（算签名、交付值）
///     hole.h                 前驱占位符：节点 sender 里「这里是前驱的值」的标记，以及找它、数它、换掉它
///     node_env.h             节点运行环境：节点看到的环境：取值源句柄 + 可选停止令牌 + 外层转发
///     node_sender.h          节点变 sender：把占位符喂给节点载荷得到节点 sender，并检查占位符恰好用一次
///   graph/       图
///     description.h          图的描述：纯数据：有哪些节点、谁依赖谁；tuple-like 协议（对算法标签泛型）
///     readers.h              值的读者：谁读谁的值 → 每个值是共享还是独占
///     node_signature.h       节点的签名：第 I 个节点在某个环境下的完成签名（只是对节点 sender 的一次查询）
///     analysis.h             节点完成分析：每个节点可能怎么完成：有值 / 有错误 / 会停止，各是什么类型
///     storage_plan.h         存储计划：哪些值要存进整图、存的时候会不会抛、唯一汇点是否直接转发
///     pred_sigs.h            前驱的签名：占位符的签名 = 前驱节点的值签名（共享的给 const&）；实现取值源的「签名接口」
///     visibility.h           节点所见环境：节点实际看到哪个环境（要不要自带停止令牌），以及各节点签名在其中是否都合法
///     slots.h                值槽与交付：前驱的值存哪里、怎么按序交给读者；实现取值源的「交付接口」
///     completions.h          图的完成签名：在给定外层环境下，整张图的值 / 错误类型与完成签名（节点不合法时原样透传）
///     node_domain.h          节点的完成域：一个节点在某个环境下的值完成域，以及一组节点的公共值完成域
///     attributes.h           图的属性：图的完成域与完成行为（值完成在汇点，失败可能来自任一节点）
///   execution/   怎么执行
///     completion_state.h     完成状态机：运行中 / 错误 / 停止；第一个错误胜出；外层停止请求怎么传
///     fused.h                融合执行：整张图作为一个 operation state 怎么调度：根、后继、入度计数、汇点
///   assembly/    拼装与入口
///     graph_sender.h         图 sender：把 描述 + 完成签名 + 属性 + 执行 拼成一个 stdexec sender；并保证取值源的两个接口都有实现者
///     dag.h                  公开入口：dag / make_graph；定义算法标签 dag_t
///
/// 有反射与模板两种实现的文件，用 SDAG_REFLECTION 隔开（见 config.h）。

#include "config.h"
#include "general/type_list.h"
#include "general/sequence.h"
#include "general/tuple.h"
#include "general/digraph.h"
#include "general/cvref.h"
#include "general/signatures.h"
#include "general/domain_merge.h"
#include "stdexec/expressions.h"
#include "stdexec/dependent.h"
#include "node/node.h"
#include "node/node_graph.h"
#include "protocol/value_source.h"
#include "protocol/hole.h"
#include "protocol/node_env.h"
#include "protocol/node_sender.h"
#include "graph/description.h"
#include "graph/readers.h"
#include "graph/node_signature.h"
#include "graph/analysis.h"
#include "graph/storage_plan.h"
#include "graph/pred_sigs.h"
#include "graph/visibility.h"
#include "graph/slots.h"
#include "graph/completions.h"
#include "graph/node_domain.h"
#include "graph/attributes.h"
#include "execution/completion_state.h"
#include "execution/fused.h"
#include "assembly/graph_sender.h"
#include "assembly/dag.h"
