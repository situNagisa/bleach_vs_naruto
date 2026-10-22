#pragma once

/// 前向声明。
///
/// 头文件之间有两处环：
///   - 编译期计算层要认得 input_sender 与 graph_sender（数输入叶子时遇到它们要停下），而它们的定义又依赖计算层；
///   - graph_sender 的成员要用到属性、完成签名与融合 op-state，而这些都以 graph_sender 为参数。
/// 都只在模板实例化时才需要完整定义，所以在这里统一声明，各文件按依赖顺序定义。

#include <cstddef>

#include "config.h"

namespace sdag
{

/// 图算法的标签（dag.h）。
struct dag_t;

/// 节点的输入 sender（node_input.h）。Dependency 是节点的 after<...>。
template<class Dependency>
struct input_sender;

inline namespace SDAG_ABI_NAMESPACE
{

/// 图 sender（graph_sender.h）：按 [dag_t, Data, 节点 sender...] 结构化绑定。
template<class Data, class... Children>
struct graph_sender;

}

namespace graph_detail
{

/// 图 sender 的属性（attributes.h）。
template<class Data, class... Children>
struct attrs;

/// 图 sender 的完成签名（attributes.h）。Self 是图 sender 的引用类型。
template<class Self, class... Env>
[[nodiscard]] consteval auto completions_of();

}

namespace fused_detail
{

/// 融合 op-state（fused.h）。Self 是图 sender 的引用类型。
template<class Self, class Receiver>
struct fused_operation;

}

}
