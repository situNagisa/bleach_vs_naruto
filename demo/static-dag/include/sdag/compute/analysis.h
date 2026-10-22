#pragma once

/// 编译期计算层（二）：整图分析。按 SDAG_REFLECTION 选择实现，两份实现对外提供完全相同的名字（namespace sdag::compute）：
///
///   analysis<Self, Env>
///       valid                         所有节点在 Env 下都能给出完成签名
///       first_invalid_t               不合法时第一个不合法的结果（原样透传给 stdexec 诊断）；合法时为 void
///       slot_needed[J]                节点 J 的值要不要存进整图 op-state
///       direct_sink / sink_index      只有一个汇点时它的值直接转发，不存槽位
///       sends_stopped / has_errors / nothrow_store / can_fail / has_values
///       values_t<J>                   节点 J 衰变后的值类型（type_list）
///       error_types                   整图的错误类型（type_list）
///       completions                   整图的完成签名；不合法时为 void
///   input_completions_t<Self, after<...>, Env>
///       输入 sender 的完成签名：前驱的值按声明顺序拼接，共享的给 V const&、独占的给 V；
///       某个前驱没有值完成时是 completion_signatures<>；前驱签名算不出时是那个不合法的结果
///   lowered<Self, FanIn, FanOut>
///       降解后每个节点的类型：input_t<I>（fanin(前驱...) 或 just()）、node_sender_t<I>、
///       built_t<I>（出度 > 1 时是扇出后的 sender）、storage_t（tuple<optional<built_t<I>>...>）
///   common_domain_t<type_list<Domain...>>
///       一组 domain 的公共 domain（与 stdexec 规则一致：相同、common_type、带 default_domain 的 common_type、indeterminate_domain）
///   common_completion_domain_t<Env, type_list<Node...>>
///       一组节点在 Env 下 set_value 完成域的公共域；有节点给不出时为 void

#include "../config.h"
#include "../graph_traits.h"

#if SDAG_REFLECTION
#include "reflection/analysis.h"
#else
#include "templates/analysis.h"
#endif
