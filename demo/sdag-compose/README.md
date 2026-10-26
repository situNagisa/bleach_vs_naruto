# sdag-compose —— 把 sdag 的图降解成扇入 / 扇出算法

依赖 [sdag](../static-dag)（header-only），sdag 不认识本库。把 `::sdag::dag(...)` 建出的图改写成由现成的扇入 / 扇出算法
（默认 `when_all` + `split`）拼成的普通 sender 树。不追求 0 开销（`split` 要堆分配共享状态），用途是语义参照、
扇入扇出可配置，以及作为 [sdag-adaptive](../sdag-adaptive) 的底座。

```cpp
#include <sdag/compose/compose.h>

auto lowered = ::sdag::compose::lower(::sdag::dag(nodes...), ::stdexec::when_all, ::exec::split);
auto g = ::sdag::compose::composed{fanin, fanout}(nodes...);   // 建图并立即降解
```

降解后的形状：

```
有数据依赖的节点 = 原节点 sender，其中的占位符换成 fanin(数据前驱 1, 数据前驱 2, ..., [then(fanin(控制前驱...), 丢掉值)])
只有控制依赖的节点 = let_value(fanin(控制前驱...), 常量 sender)
出度 > 1 的节点先经过 fanout，各后继拿到它的拷贝；出度 1 的节点直接 move 给后继
整图 = fanin(汇点...)
```

`fanin` 要能接收 1 个或多个 sender（1 个时原样返回，`when_all` 正是这样），`fanout` 接收 1 个 sender 并返回可拷贝的 sender。
注意 `when_all` 会把 `split` 给的 `const&` 再拷贝一份存进自己的 op-state，降解后的共享值到后继时是右值；融合路径没有这次拷贝。
出度按全部后继（`data<>` 与 `control<>`）计算，所以一个前驱有一个 `data<>` 后继、一个 `control<>` 后继时也会经过 `split`。

降解要求节点 sender 由 stdexec 的 sexpr 算法组成（`then`、`let_value`、`continues_on`……都是），这样才能把占位符替换进去；
不透明的自定义 sender 包着占位符时 sdag 建图那一步就有一条 `static_assert`。

## 文件

```
include/sdag/compose/
  expressions.h   由 [tag, data, child...] 重建 stdexec 的算法 sender——本库对 stdexec 内部实现的全部依赖（__sexpr）
  hole_fill.h     hole_filled_t / fill_hole：把节点 sender 里的占位符换成输入 sender（反射 / 模板两份实现）
  lowering.h      lowered_types（反射版按拓扑序一次填好 lowered_tables 查表）、builder、lower
  compose.h       composed，总头
test/compose_test.cpp   降解与融合结果一致、扇入扇出可配置、data + control 的降解
```

用到的 sdag 内部接口（待 sdag 整理出正式的公开接口后替换）：`graph::description_of` / `shape_of_v` / `child_t` / `node_sender_t`、
`graph::data_dependencies_t` / `control_dependencies_t`、图 sender 的 `get<I>`，以及 `protocol::hole`、`hole_count_v`、`stdexec_adapter::parts_t` / `apply`。

## 构建

与 sdag 相同的工具链与预设，sdag 经 `add_subdirectory(../static-dag)` 引入（`-DSDAG_COMPOSE_SDAG_DIR=...` 可改）。

```bash
cmake --preset linux-clang-release && cmake --build --preset linux-clang-release && ctest --preset linux-clang-release
```
