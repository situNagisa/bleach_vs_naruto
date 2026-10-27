#pragma once

/// sdag：静态 DAG sender。
///
///   enum class k { a, b, c, d };
///   auto g = ::sdag::dag<k>(
///       ::sdag::make<::sdag::basic_node{k::a}>(::stdexec::just(1)),
///       ::sdag::make<::sdag::basic_node{k::b, ::sdag::let(k::a)}>(::stdexec::then([](int const& x) { return x + 1; })),
///       ::sdag::make<::sdag::basic_node{k::c, ::sdag::let(k::a)}>(::stdexec::then([](int const& x) { return x * 2; })),
///       ::sdag::make<::sdag::basic_node{k::d, ::sdag::move(k::b), ::sdag::move(k::c)}>(::stdexec::then([](int y, int z) { return y + z; })));
///   static_assert(::stdexec::sender<decltype(g)>);
///   auto [sum] = ::stdexec::sync_wait(::std::move(g)).value();   // 4
///
/// 目录对应概念分组（general 与图无关 → stdexec 适配 → node → protocol → graph → execution → assembly），
/// 文件对应模块，每个文件开头一句话说明它是什么；下层不认识上层。
///
/// 有反射与模板两种实现的文件，用 SDAG_REFLECTION 隔开（见 config.h）。

#include "config.h"
#include "general/type_list.h"
#include "general/sequence.h"
#include "general/tuple.h"
#include "general/relay.h"
#include "general/cvref.h"
#include "general/signatures.h"
#include "general/domain_merge.h"
#include "topology/digraph.h"
#include "topology/chains.h"
#include "topology/dependencies.h"
#include "stdexec/expressions.h"
#include "node/node.h"
#include "node/node_graph.h"
#include "protocol/value_source.h"
#include "protocol/hole.h"
#include "protocol/node_env.h"
#include "protocol/node_sender.h"
#include "graph/description.h"
#include "graph/analysis.h"
#include "graph/storage_plan.h"
#include "graph/pred_sigs.h"
#include "graph/visibility.h"
#include "graph/slots.h"
#include "graph/completions.h"
#include "graph/node_domain.h"
#include "graph/attributes.h"
#include "execution/fused.h"
#include "assembly/graph_sender.h"
#include "assembly/dag.h"
