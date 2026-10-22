#pragma once

/// 前向声明：编译期计算层要认得图 sender 与输入 sender，但它们的定义依赖计算层。

#include <cstddef>

#include "config.h"

namespace sdag
{

/// 图算法的标签。
struct dag_t;

/// 节点的输入 sender（Dependency 就是节点的 after<...>）。
template<class Dependency>
struct input_sender;

inline namespace SDAG_ABI_NAMESPACE
{

/// 图 sender：按 [dag_t, Data, 节点 sender...] 结构化绑定。
template<class Data, class... Children>
struct graph_sender;

}

}
