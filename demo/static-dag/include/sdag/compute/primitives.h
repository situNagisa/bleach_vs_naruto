#pragma once

/// 编译期计算层（一）：类型列表、完成签名、sender 表达式。按 SDAG_REFLECTION 选择实现，
/// 两份实现对外提供完全相同的名字（namespace sdag::compute）：
///
///   类型列表        index_of_v<T, List>、concat_t<Lists...>、unique_t<List>、
///                   transform_t<Fn, List>、rebind_t<To, List>
///   常量数组        sequence_t<Array>（std::index_sequence<Array[i]...>）、
///                   for_each_value<Array>(fn)（依次调用 fn.template operator()<Array[i]>()）
///   依赖            dependency_indices_v<Keys, after<...>>（键换成节点索引，std::array）
///   完成签名        is_completion_signatures_v<T>、completion_summary<Completions>、
///                   make_completions_t<HasValues, Values, Errors, SendsStopped>、value_completions_t<Values>
///   sender 表达式   input_count_v<Sender>（输入叶子个数，不进入嵌套的图）、
///                   substituted_t<Sender, Input>（输入叶子换成 Input 之后的类型）

#include "../config.h"

#if SDAG_REFLECTION
#include "reflection/primitives.h"
#else
#include "templates/primitives.h"
#endif
