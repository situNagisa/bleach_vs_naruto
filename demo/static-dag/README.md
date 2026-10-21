# sdag —— 静态 DAG sender

一个只依赖 `std` 与 [stdexec](https://github.com/NVIDIA/stdexec) 的 header-only 模板库：
把一组各自声明了依赖的 sender 节点整理成一张 **编译期确定** 的有向无环图，整张图本身就是一个 sender，
完成语义与 `when_all` 同一套理念。默认实现是 0 开销的：整图一个 operation state，没有堆分配、
没有引用计数、没有类型擦除。

```cpp
#include <sdag/sdag.h>

struct load {};  struct parse {};  struct index {};  struct report {};

auto g = ::sdag::dag(
	::sdag::make_node<load>(::stdexec::just(::std::string{"..."})),
	::sdag::make_node<parse, ::sdag::after<load>>(::stdexec::then([](::std::string const& text) { return text.size(); })),
	::sdag::make_node<index, ::sdag::after<load>>(::stdexec::then([](::std::string const& text) { return text.front(); })),
	::sdag::make_node<report, ::sdag::after<parse, index>>(::stdexec::then([](::std::size_t n, char c) { return ::std::format("{}x{}", n, c); })));

static_assert(::stdexec::sender<decltype(g)>);
auto [text] = ::stdexec::sync_wait(::std::move(g)).value();
```

纯类型写法也成立，对应最初设想的 `xxx<pack<sndr1, fanin<sndr2, sndr3>>, ...>`：

```cpp
using root = ::sdag::node<load, ::sdag::after<>, decltype(::stdexec::just(1))>;
using next = ::sdag::node<parse, ::sdag::after<load>, decltype(::stdexec::then([](int) { return 1.0; }))>;
using dag = ::sdag::dag_sender<::sdag::fused, root, next>;
static_assert(::stdexec::sender<dag>);
```

## 节点

| 写法 | 含义 |
| --- | --- |
| `make_node<key>(sender)` | 根节点，直接启动 |
| `make_node<key, after<a, b>>(adaptor)` | 数据依赖：`adaptor` 是 sender 适配器（`then(f)`、`let_value(f)`、`continues_on(sched) \| then(f)` ……），喂给它的输入 sender 完成值是 `(a 的值..., b 的值...)` |
| `make_node<key, after<a>>(sender)` | 控制依赖：只在 `a` 之后启动，不接收它的值 |

`key` 只是编译期身份，任何类型都行；`after<...>` 里的顺序就是值的顺序。每个节点至多一种 `set_value` 完成（多种先 `into_variant`），这与 `when_all` 的要求一致。

自定义节点类型只要满足三个原语：`node_key<N>::type`、`node_dependencies<N>::type`（可特化的类型特征，默认取嵌套的 `key_type` / `dependencies_type`）和 `payload(n)`（CPO，成员优先、其次 ADL）。`graph_node` concept 组合它们。

## 完成语义（与 `when_all` 一致）

- 全部成功：`set_value(汇点 1 的值..., 汇点 2 的值..., ...)`，汇点（没有后继的节点）按声明顺序。
- 任一节点 `set_error`：向所有在飞节点请求停止；**还没启动的后继直接跳过**；全部结束后以第一个错误完成。
- 任一节点 `set_stopped`：同上，以 `set_stopped` 完成；错误优先于停止。
- 外层停止令牌：图能以 `stopped` 完成时切换到停止态；否则只把请求转给节点，图仍以它们的值完成。
- 值的传递：**只有一个后继的前驱把值 move 给它**（签名按值），**多个后继的前驱按 `V const&` 共享**。汇点的值 move 给整图的接收者。
- 存值时抛异常（拷贝 / 移动构造抛）会变成 `set_error(exception_ptr)`。
- 任一节点没有 `set_value` 完成（例如 `just_stopped()`），整图也没有 —— 每个节点都是某个汇点的祖先。

节点接收者的环境：`get_stop_token` 是整图的 `inplace_stop_token`，其余 **转发查询** 原样来自外层接收者（调度器、分配器……都能透传）。

## 两种策略

```cpp
::sdag::dag(nodes...)                                // 默认 fused
::sdag::dag(::sdag::fused{}, nodes...)
::sdag::dag(::sdag::composed{::stdexec::when_all, ::exec::split}, nodes...)
```

### `fused`（默认，0 开销）

整张图融合成一个 operation state：

- 每个节点的 op-state 是整图 op-state 的直接成员（`connect` 的纯右值原地构造，没有移动）；
- 节点的值存一份在整图里，后继按引用读 —— **扇出不需要 `split`**，因为整图拥有所有节点，不存在"谁最后释放"的问题；
- 入度 ≥ 2 的节点有一个原子倒计数，入度 1 的节点由前驱直接启动 —— **扇入不需要 `when_all`**；
- 不会失败 / 停止 / 抛异常的图连停止源、状态字、错误槽都不存在（与 `when_all` 的 `__uses_stop_source` 同理）；只有一个汇点的图没有汇点计数；值没人读的节点没有值槽。被优化掉的成员是真正的 0 字节。

`test/zero_overhead.cpp` 的实测（clang 22，libc++，Release）：

| | 分配次数 | `sizeof(op-state)` |
| --- | --- | --- |
| 菱形 `a -> {b, c} -> d`，fused | **0** | 112 = 节点 op-state 64 + 接收者 8 + 4 个值槽 32 + 1 个入度计数 8 |
| 同一张图，composed（`when_all` + `split`） | 1（split 的共享状态） | —— |

`-O2` 下 `just(1) -> then(+1) -> then(*3)` 这条链的三个节点全部折叠成常量 1、2、6，剩下的指令是 `sync_wait` 自己的 `run_loop`，与手写 `just(1) | then | then` 相同。

### `composed<FanIn, FanOut>`

用现成的扇入 / 扇出算法把图拼成一棵普通的 sender 表达式树：节点 sender = `载荷(fanin(前驱...))`，
出度 > 1 的节点先 `fanout`，整图 = `fanin(汇点...)`。它是语义参照（测试里两种策略互相对照），也是"扇入扇出可配置"的入口：
`fanin` 要能接收 1 个或多个 sender（1 个时原样返回，`when_all` 正是这样），`fanout` 接收 1 个 sender 并返回可拷贝的 sender。
`split` 会堆分配，这是组合现成 combinator 的固有代价。

## 文件

```
include/sdag/
  meta.h        类型列表、indexed_tuple（按工厂原地构造、空元素 0 字节）
  signatures.h  completion_signatures 的拆解与拼装
  node.h        after<>、node<>、make_node、原语（类型特征 + payload CPO）与 concept
  shape.h       编译期形状分析：邻接矩阵、入出度、拓扑序、错误诊断（shape_of 不断言，checked_shape 断言）
  fused.h       融合策略：fused_sender / 整图 op-state
  composed.h    组合策略：composed<FanIn, FanOut>
  dag.h         dag()、graph_policy_for、dag_sender
  sdag.h        总头
test/
  test.cpp           编译期 + 运行期测试（两种策略、错误 / 停止传播、线程池、48 节点长链、16 路扇入）
  zero_overhead.cpp  分配计数与 op-state 大小
```

## 构建

只依赖 stdexec 与 pthread；只支持 Clang ≥ 22（Linux 上 clang++ + libc++，Windows 上 clang-cl + MSVC STL），C++26，Ninja。

```bash
cmake --preset linux-clang-release
```

```bash
cmake --build --preset linux-clang-release && ctest --preset linux-clang-release
```

stdexec 的位置：`$NAGISA_LIBRARY_ROOT/stdexec`，或 `-DSDAG_STDEXEC_ROOT=...`；都没有时 FetchContent 拉取验证过的提交。
本库在 stdexec `8398d5a`（2026-10-20）上验证。

## 实现要点与边界

- **编译期深度**。`then(f)` 套在输入 sender 上时 stdexec 会立刻算一次完成签名，节点 N 的类型计算会嵌套进节点 N-1 的，长链会撞上 clang 的模板深度上限。`fused.h` 里的 `warm_up` 按拓扑序逐个实例化节点签名，让后面的计算全部命中编译器缓存，嵌套深度变成常数。48 节点长链 + 16 路扇入的测试文件在本机编译约 40 s —— 这是模板实例化总量的代价，不是深度的。
- **内联完成的栈深度**。节点在前驱的完成回调里被启动，一条全内联的链会递归 O(深度) 层；静态图的规模是编译期已知的常量，这与 `then` 链的行为一致。
- **节点的值在整图 op-state 里停留到整图完成**。共享的 `const&` 因此一直有效；这也是 `split` 做不到 0 开销而整图可以的原因。
- **错误发生后未启动的节点被跳过**，这是对 `when_all`"全部启动"语义的细化：跳过不改变结果（结果一定是错误 / 停止），只是不做无用功。
- 节点必须恰好完成一次；完成签名不能"撒谎"（`deliver` 里有 `static_assert` 对照接收者环境与整图环境下算出的输入签名）。
- 诊断：重复键、未知依赖、自依赖、重复依赖、成环各是一条 `static_assert`；`shape_of<Nodes...>()` 不断言，返回 `shape_error`，便于测试。
