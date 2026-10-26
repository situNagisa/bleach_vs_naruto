# sdag-adaptive —— 自适配图

依赖 [sdag](../static-dag) 与 [sdag-compose](../sdag-compose)（header-only），两者都不认识本库。

`::sdag::adaptive::adaptive_dag(nodes...)` 建一张**自适配图**：平时与 `::sdag::dag` 一样连接成融合 op-state（0 开销）；
所在 domain 定制了 `when_all` 时（例如 GPU 的 stream domain），自动用 sdag-compose 降解成 `when_all` / `split` / `let_value`，
让 domain 拿到它认识的标准算法，整条链留在它的调度器上，不会在每条边上退回宿主同步。

```cpp
#include <sdag/adaptive/adaptive.h>

auto g = ::sdag::adaptive::adaptive_dag(
    ::sdag::make_node<a>(::stdexec::just(1)),
    ::sdag::make_node<b, ::sdag::data<a>>(::stdexec::then([](int x) { return x + 1; })));
```

自适配图用自己的标签 `adaptive_dag_t`（由 `::sdag::make_graph<adaptive_dag_t>` 建出），触发点是这个标签的 `transform_sender`。
`::sdag::dag` 建的图标签是 `dag_t`，没有 `transform_sender`，在任何 domain 下都融合——要不要自适配在建图时选定。

## 什么时候降解

`adaptive_dag_t::transform_sender(OpTag, g, env)` 只在"这一阶段的 domain 会定制覆盖在汇点上的 `when_all`"时才存在
（`start_t` 阶段看 `get_domain(env)`，`set_value_t` 阶段看图自己报告的完成域）。判据：把各汇点 sender 里的占位符换成
一个"完成签名与真实占位符一致"的占位 sender，拼成 `when_all<汇点...>` 做探针，看这个 domain 的 `transform_sender`
会不会改写它（结果与 `default_domain` 不同）。会，就 `compose::lower(g, when_all, split)`。

默认 domain 和只定制 `bulk` 的线程池 domain 都不会触发降解。测试用一个只定制 `when_all` 的 mock domain 验证了降解发生、
结果一致；同一个 domain 下 `::sdag::dag` 建的图不降解；线程池上仍走融合。节点签名在环境下算不出来时判据为假，不硬错误。

## 文件

```
include/sdag/adaptive/
  domain_custom.h     phase_domain / customizes：某个 domain 在某个阶段会不会改写某个 sender
  lowering_policy.h   lowering_wanted：when_all 探针
  adaptive.h          adaptive_dag_t、adaptive_dag，总头
test/adaptive_test.cpp
```

用到的 sdag 内部接口（待 sdag 整理出正式的公开接口后替换）：`make_graph<Tag>`、`graph::graph_state`、`graph::node_env_t`、
`graph::pred_completions_t`、`graph::evaluable_in`、`graph::child_t` / `data_dependency_t` / `shape_of_v`、`protocol::hole_attrs`。

## 构建

与 sdag 相同的工具链与预设，sdag-compose 经 `add_subdirectory(../sdag-compose)` 引入（`-DSDAG_ADAPTIVE_COMPOSE_DIR=...` 可改）。

```bash
cmake --preset linux-clang-release && cmake --build --preset linux-clang-release && ctest --preset linux-clang-release
```
