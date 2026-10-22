# sdag —— 静态 DAG sender

一个只依赖 `std` 与 [stdexec](https://github.com/NVIDIA/stdexec) 的 header-only 模板库：
把一组各自声明了依赖的 sender 节点整理成一张 **编译期确定** 的有向无环图。整张图是一个带 `dag_t`
标签的 stdexec sender 表达式（`__sexpr`），子 sender 就是用户给的节点；完成语义与 `when_all`
同一套理念。默认连接走 0 开销的融合 operation state：没有堆分配、没有引用计数、没有类型擦除。
所在 domain 定制了 `when_all` 时，图自动降解成 `when_all` / `split` / `let_value` 组合交给 domain。

```cpp
#include <sdag/sdag.h>

struct load {};  struct parse {};  struct index {};  struct report {};

auto g = ::sdag::dag(
	::sdag::make_node<load>(::stdexec::just(::std::string{"..."})),
	::sdag::make_node<parse, ::sdag::after<load>>(::stdexec::then([](::std::string const& text) { return text.size(); })),
	::sdag::make_node<index, ::sdag::after<load>>(::stdexec::then([](::std::string const& text) { return text.front(); })),
	::sdag::make_node<report, ::sdag::after<parse, index>>(::stdexec::then([](::std::size_t n, char c) { return ::std::format("{}x{}", n, c); })));

static_assert(::stdexec::sender<decltype(g)>);
static_assert(::exec::sender_for<decltype(g), ::sdag::dag_t>);
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

## 它在 stdexec 生态里的位置

`dag(nodes...)` 返回 `::stdexec::__sexpr<dag_t, dag_data<键表, 依赖表>, 节点 sender...>`：

- 节点 sender 原样保留在 children 里：`then(f)` 节点是 `then(input_sender<after<...>>, f)`，`continues_on(sched) | then(f)` 节点也是对应的表达式树。它们连接时照常被所在 domain 变换——`then`、`bulk`、`continues_on` 这些标准算法吃到的定制一个不少。
- `tag_of_t` / `exec::sender_for<G, dag_t>` / 子节点内省都成立；domain 可以像定制任何算法一样在 `transform_sender` 里匹配 `dag_t`。
- 属性（`get_env(g)`）按 `when_all` 的 `__attrs` 同一套语义回答 `get_completion_domain<...>` 与完成行为：图在汇点完成的地方完成。
- `input_sender<after<...>>` 是一个像 `read_env` 一样"依赖环境"的叶子 sender：完成签名来自接收者环境里的整图状态类型。所以图在无环境下是 dependent sender（`sender` 成立，`sender_in<G>` 不成立，`sender_in<G, env>` 成立）。

### 默认连接：融合 op-state（0 开销）

`__sexpr_impl<dag_t>::__connect` 返回一个把整张图融合在一起的 operation state：

- 每个节点的 op-state 是整图 op-state 的直接成员（`connect` 的纯右值原地构造，没有移动）；
- 节点的值存一份在整图里，后继按引用读 —— **扇出不需要 `split`**，因为整图拥有所有节点，不存在"谁最后释放"的问题；
- 入度 ≥ 2 的节点有一个原子倒计数，入度 1 的节点由前驱直接启动 —— **扇入不需要 `when_all`**；
- 不会失败 / 停止 / 抛异常的图连停止源、状态字、错误槽都不存在（与 `when_all` 的 `__uses_stop_source` 同理）；外层令牌是 `unstoppable_token` 时连回调槽也没有；只有一个汇点的图没有汇点计数，**唯一汇点的值不存槽位，完成时直接转发给整图的接收者**（像 `then` 一样不物化，整图的值签名就是它的原始签名）；值没人读的节点没有值槽。被优化掉的成员是真正的 0 字节（MSVC ABI 下用 `[[msvc::no_unique_address]]` 与 `__declspec(empty_bases)`）。
- 共享还是独占按"读值的后继个数"判断：只有控制依赖的后继不算，所以 `a -> b`（读值）、`a -> c`（只等先后）时 `a` 的值仍然 move 给 `b`。

`test/zero_overhead.cpp` 的实测（clang 22，libc++，Release）：

| | 分配次数 | `sizeof(op-state)` |
| --- | --- | --- |
| 菱形 `a -> {b, c} -> d`，融合 | **0** | 104 = 节点 op-state 64 + 接收者 8 + 3 个值槽 24 + 1 个入度计数 8（汇点 d 不存槽位） |
| 同一张图，降解（`when_all` + `split`） | 1（split 的共享状态） | —— |

`-O2` 下 `just(1) -> then(+1) -> then(*3)` 这条链的三个节点全部折叠成常量 1、2、6，剩下的指令是 `sync_wait` 自己的 `run_loop`，与手写 `just(1) | then | then` 相同。
相对手写 `then` 链，融合路径唯一多出来的是**链式边上的一次值物化**（出度 1 → 入度 1 的值先存进槽位再 move 给后继）：后继的输入 op 什么时候启动是后继自己决定的（`starts_on`、`let_value` 都可能推迟），值必须有个地方待着。这是"与扇入 / 扇出同等调度的最小成本"，不是 `then` 链的成本。

### 自动降解：让定制了 `when_all` 的 domain 接管

`dag_t::transform_sender(OpTag, g, env)` 只在"这一阶段的 domain 会定制覆盖在汇点上的 `when_all`"时才存在
（`start_t` 阶段看 `get_domain(env)`，`set_value_t` 阶段看图自己报告的完成域）。判据：把各汇点 sender 里的输入叶子换成
一个"完成签名与真实输入一致"的占位 sender，拼成 `when_all<汇点...>` 做探针，用 `__default_domain_like` 看这个 domain 的
`transform_sender` 会不会改写它。存在时整图降解为：

```
节点 sender = 原节点 sender，其中的 input_sender 叶子换成 fanin(前驱 1, 前驱 2, ...)
出度 > 1 的节点先经过 fanout，各后继拿到它的拷贝；出度 1 的节点直接 move 给后继
只有控制依赖的节点 = let_value(fanin(前驱...), 常量 sender)
整图 = fanin(汇点...)
```

于是 GPU 这类 domain 看到的是它认识的 `when_all` / `split` / `then`，整条链留在它的调度器上，不会在每个节点上退回宿主同步。
默认 domain 和只定制 `bulk` 的线程池 domain 都不会触发降解，仍然走融合路径——测试里用一个只定制 `when_all` 的 mock domain
验证了降解发生、结果一致，并用线程池验证了不降解。

### `composed<FanIn, FanOut>` 策略与 `lower`

`::sdag::lower(g, fanin, fanout)` 是上面降解的手动入口；`composed{when_all, split}` 策略就是 `lower(dag_t{}(nodes...), ...)`。
它是语义参照（测试里两种路径互相对照），也是"扇入扇出可配置"的入口：`fanin` 要能接收 1 个或多个 sender（1 个时原样返回，`when_all` 正是这样），
`fanout` 接收 1 个 sender 并返回可拷贝的 sender。注意 `when_all` 会把 `split` 给的 `const&` 再拷贝一份存进自己的 op-state，降解后的共享值到后继时是右值；融合路径没有这次拷贝。

降解要求节点 sender 由 stdexec 的 sexpr 算法组成（`then`、`let_value`、`continues_on`……都是），这样才能把输入叶子替换进去；不透明的自定义 sender 包着输入时会有一条 `static_assert`。

## 文件

```
include/sdag/
  meta.h        类型列表、indexed_tuple（按工厂原地构造、空元素 0 字节）
  signatures.h  completion_signatures 的拆解与拼装
  node.h        after<>、node<>、make_node、原语（类型特征 + payload CPO）与 concept
  shape.h       编译期形状分析：邻接矩阵、入出度、拓扑序、错误诊断（shape_of 不断言，checked_shape_of_lists 断言）
  graph.h       图 sender：dag_t、input_sender、节点环境、整图状态与分析、attrs、降解判据、__sexpr_impl<dag_t>
  fused.h       融合 op-state（默认连接）与 fused 策略
  composed.h    lower（输入叶子替换 + when_all/split/let_value 组合）与 composed 策略
  dag.h         dag()、graph_policy_for、dag_sender
  sdag.h        总头
test/
  test.cpp           编译期 + 运行期测试（两种路径、错误 / 停止传播、线程池、48 节点长链、16 路扇入、生态接入）
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

- **用到的 stdexec 作者级接口**。`__sexpr` / `__sexpr_impl` / `__get<I>` / `__copy_cvref_t` / `__throw_dependent_sender_error` / `__has_transform_sender` / `__completion_domain_of_t` 等带双下划线的名字是 stdexec 给算法作者用的那一层（`exec::split`、`exec::when_any` 自己也这么写）。本库在 stdexec `8398d5a` 上验证；这一层升级时可能要跟着改。
- **编译期深度**。`then(f)` 问子 sender 的签名时，节点 N 的类型计算会嵌套进节点 N-1 的，长链会撞上 clang 的模板深度上限。`graph.h` 里的 `warm_up` 按拓扑序逐个实例化节点签名，让后面的计算全部命中编译器缓存，嵌套深度变成常数。48 节点长链 + 16 路扇入的测试文件在本机编译约 40 s —— 这是模板实例化总量的代价，不是深度的。
- **内联完成的栈深度**。节点在前驱的完成回调里被启动，一条全内联的链会递归 O(深度) 层；静态图的规模是编译期已知的常量，这与 `then` 链的行为一致。
- **节点的值在整图 op-state 里停留到整图完成**。共享的 `const&` 因此一直有效；这也是 `split` 做不到 0 开销而整图可以的原因。
- **错误发生后未启动的节点被跳过**，这是对 `when_all`"全部启动"语义的细化：跳过不改变结果（结果一定是错误 / 停止），只是不做无用功。
- 节点必须恰好完成一次；完成签名不能"撒谎"（`deliver` 里有 `static_assert` 对照接收者环境与整图环境下算出的输入签名）。
- 节点适配器必须把输入 sender 恰好用一次（构造时 `static_assert`）：前驱的值只交付一次，`repeat_n` / `retry` 这类会重启子 sender 的适配器不能套在输入上——这与把它们套在 `just(std::move(x))` 上会重复移动是同一回事。
- 外层停止回调只做一次状态切换和一次 `request_stop`，之后不再碰 op-state：`inplace_stop_source::request_stop` 自己处理"回调里把 source 销毁了"的情形，整图可能就在这次 `request_stop` 里同步完成。
- 诊断：重复键、未知依赖、自依赖、重复依赖、成环各是一条 `static_assert`；`shape_of<Nodes...>()` 不断言，返回 `shape_error`，便于测试。
