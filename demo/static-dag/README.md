# sdag —— 静态 DAG sender

一个只依赖 `std` 与 [stdexec](https://github.com/NVIDIA/stdexec) 的 header-only 库：
把一组各自声明了依赖的 sender 节点整理成一张 **编译期确定** 的有向无环图。整张图是一个以 `dag_t`
为标签、可按 `[tag, data, child...]` 结构化绑定的 sender（与 stdexec 的算法 sender 同一个形状），
子 sender 就是用户给的节点；完成语义与 `when_all` 同一套理念。默认连接走 0 开销的融合 operation state：
没有堆分配、没有引用计数、没有类型擦除。图本身也可以作为另一张图的节点。

编译期计算有两份实现，由 `SDAG_REFLECTION` 选择：编译器支持 **C++26 静态反射（P2996）+ 展开语句**
时（目前是 GCC ≥ 16 加 `-freflection`）用反射写成的 `consteval` 函数；否则（clang 22 / 23 等）用模板元编程。
两份实现对外接口相同，运行期代码只有一份，同一编译器下生成的机器码逐行相同。

```cpp
#include <sdag/sdag.h>

enum class step { load, parse, index, report };

using ::sdag::basic_node;  using ::sdag::let;  using ::sdag::move;

auto g = ::sdag::dag<step>(
	::sdag::make<basic_node{step::load}>(::stdexec::just(::std::string{"..."})),
	::sdag::make<basic_node{step::parse, let(step::load)}>(::stdexec::then([](::std::string const& text) { return text.size(); })),
	::sdag::make<basic_node{step::index, let(step::load)}>(::stdexec::then([](::std::string const& text) { return text.front(); })),
	::sdag::make<basic_node{step::report, move(step::parse), move(step::index)}>(::stdexec::then([](::std::size_t n, char c) { return ::std::format("{}x{}", n, c); })));

static_assert(::stdexec::sender<decltype(g)>);
static_assert(::exec::sender_for<decltype(g), ::sdag::dag_t<step>>);
auto [text] = ::stdexec::sync_wait(::std::move(g)).value();
```

纯类型写法也成立：`::sdag::dag_sender<step, decltype(::sdag::make<...>(...))...>`。

## 节点

一个节点 = 规格（编译期的**值**）+ 载荷。`dag<K>` 在建图时给出键类型 `K`；规格只要满足概念 `node_spec_of<N, K>`：

- `::sdag::id(n)` 给出 `K`；`::sdag::dependencies(n)` 给出一个 range，正好是这个节点的全部依赖；
- range 的每个元素 `e`：`::sdag::id(e)` 给出前驱的键，`::sdag::mode_of(e)` 给出 `::sdag::mode`；
- 这三个都是 CPO（成员函数优先，其次 ADL 的自由函数），所以用户可以用自己的类型。

库给的便利类型是 `basic_node{id, let(a), move(b), wait(c)}`，依赖个数由构造推导（不定长）。

| 依赖 | 含义 |
| --- | --- |
| `let(a)` | 读：载荷拿到 `a` 的 `V const&`，值留在整图里、活到整图结束 |
| `move(a)` | 取走：载荷按值（move）拿到 `a` 的值，本节点结束时析构它，之后它占的空间可以给别的值复用 |
| `wait(a)` | 只在 `a` 之后启动，不接收、不碰它的值 |

`let` / `move` 的前驱按依赖里的顺序把值交给载荷（可以交错，`wait` 不占位置）。有 `let` / `move` 时载荷必须是 sender 适配器
（`then(f)`、`let_value(f)`、`continues_on(sched) | then(f)` ……），dag 把前驱占位符喂给它；否则载荷必须是 sender。写错了各有一条 `static_assert`。
一个值至多被 `move` 一次，`move` 它的节点必须排在它所有其它读者之后（编译期检查；把其它读者写进它的依赖即可）。`move` 之后，
其它读者手里指向它的视图不再有效。同一个前驱只能写一次（不管用法）。每个节点至多一种 `set_value` 完成（多种先 `into_variant`）。

## 完成语义（与 `when_all` 一致）

- 全部成功：`set_value(汇点 1 的值..., 汇点 2 的值..., ...)`，汇点（没有后继的节点）按声明顺序。
- 任一节点 `set_error`：向所有在飞节点请求停止；**还没启动的后继直接跳过**；全部结束后以第一个错误完成。
- 任一节点 `set_stopped`：同上，以 `set_stopped` 完成；错误优先于停止。
- 外层停止令牌：图能以 `stopped` 完成时切换到停止态；否则只把请求转给节点，图仍以它们的值完成。
- 值的传递：`move` 的按值交出，并在取走它的节点结束时析构；`let` 按 `V const&`，活到整图结束。汇点的值 move 给整图的接收者。
- 存值时抛异常（拷贝 / 移动构造抛）会变成 `set_error(exception_ptr)`。
- 任一节点没有 `set_value` 完成（例如 `just_stopped()`），整图也没有 —— 每个节点都是某个汇点的祖先。

节点接收者的环境：`get_stop_token` 是整图的 `inplace_stop_token`，其余 **转发查询** 原样来自外层接收者（调度器、分配器……都能透传）。

## 它在 stdexec 生态里的位置

`dag<K>(nodes...)` 返回 `::sdag::graph_sender<dag_t<K>, graph::graph_data<K, 规格...>, 节点 sender...>`（数据与 tuple-like 协议在 `graph::graph_description`，sender 行为在 `graph_sender`）。它实现标准的 tuple-like 协议
（`std::tuple_size` / `std::tuple_element` 特化 + 成员 `get<I>`），stdexec 靠结构化绑定认出它的标签，不依赖 stdexec 的内部类型：

- 节点 sender 原样保留在 children 里：`then(f)` 节点是 `then(hole<I>, f)`，`continues_on(sched) | then(f)` 节点也是对应的表达式树。它们连接时照常被所在 domain 变换——`then`、`bulk`、`continues_on` 这些标准算法吃到的定制一个不少。
- `tag_of_t` / `exec::sender_for<G, dag_t<K>>` / 子节点内省都成立；domain 可以像定制任何算法一样在 `transform_sender` 里匹配 `dag_t<K>`。
- 属性（`get_env(g)`）按 `when_all` 属性的同一套语义回答 `get_completion_domain<...>` 与完成行为：图在汇点完成的地方完成。
- 前驱占位符 `protocol::hole<I>`（第 I 个节点的） 是一个像 `read_env` 一样"依赖环境"的叶子 sender：它不认识图，只认识**取值源协议**（`protocol/value_source.h`）——接收者环境里的转发查询 `get_value_source` 给出一个取值源，签名问它的签名接口（`completions_for<Dependency, Env>`），启动时调它的交付接口（`deliver<Dependency>(receiver)`）。图这一侧由 `graph::graph_state` 实现这两个接口，于是"占位符 → 图"的环被打断。所以图在无环境下是 dependent sender（`sender` 成立，`sender_in<G>` 不成立，`sender_in<G, env>` 成立）。

### 默认连接：融合 op-state（0 开销）

`graph_sender::connect` 返回一个把整张图融合在一起的 operation state：

- 每个节点的 op-state 是整图 op-state 的直接成员（`connect` 的纯右值原地构造，没有移动）；
- 节点的值存一份在整图里，后继按引用读 —— **扇出不需要 `split`**，因为整图拥有所有节点，不存在"谁最后释放"的问题；
- 入度 ≥ 2 的节点有一个原子倒计数，入度 1 的节点由前驱直接启动 —— **扇入不需要 `when_all`**；
- 不会失败 / 停止 / 抛异常的图连停止源、状态字、错误槽都不存在（与 `when_all` 的 `__uses_stop_source` 同理）；外层令牌是 `unstoppable_token` 时连回调槽也没有；只有一个汇点的图没有汇点计数，**唯一汇点的值不存槽位，完成时直接转发给整图的接收者**（像 `then` 一样不物化，整图的值签名就是它的原始签名）；值没人读的节点没有值槽。被优化掉的成员是真正的 0 字节。
- **值槽复用**：被 move 的值在取走它的节点结束时析构，它的空间可以给"一定在那之后才产生"的值用（取走它的节点是那个值所在节点的祖先）。存值的节点在编译期按这个先后关系划分成最少的链（偏序的最小链划分，链数 = 最多同时在世的值的个数），同一条链的值依次共用一块存储（`general/relay.h`）。只有一个值的链就是原来的 `optional`；不用 move 的图布局与机器码不变。

`test/zero_overhead.cpp` 的实测（g++ 16.2，libstdc++，Release）：

| | 分配次数 | `sizeof(op-state)` |
| --- | --- | --- |
| 菱形 `a -> {b, c} -> d`，融合 | **0** | 128 = 节点 op-state 88 + 接收者 8 + 3 个值槽 24 + 1 个入度计数 8（汇点 d 不存槽位） |
| 链 `a -> b -> c -> d -> e`，每步 move，中间值 64 字节 | **0** | 值槽 128（4 个中间值共用两块；全用 let 时 272） |

`-O2` 下 `just(1) -> then(+1) -> then(*3)` 这条链经 `sync_wait` 跑完的函数是 30 条指令，手写 `just(1) | then | then` 是 29 条，节点全部折叠成常量。
相对手写 `then` 链，融合路径唯一多出来的是**链式边上的一次值物化**（值先存进槽位，再 move 给后继）：后继的输入 op 什么时候启动是后继自己决定的（`starts_on`、`let_value` 都可能推迟），值必须有个地方待着。这是"与扇入 / 扇出同等调度的最小成本"，不是 `then` 链的成本。

### 让 domain 接管整张图

`dag_t<K>` 没有 `transform_sender`，图在任何 domain 下都走融合 op-state。domain 要接管整张图，在自己的 `transform_sender`
里匹配 `dag_t<K>` 即可（`exec::sender_for<G, dag_t<K>>`，子 sender 就是节点 sender）。也可以用 `make_graph<Tag, K>(nodes...)`
以自己的标签建图，在标签上定制 `transform_sender`。

## 常量求值

图这一层的函数都标了 `constexpr`，常量求值止步于原子量与停止源：入度计数、汇点计数、状态字是 `std::atomic`，停止源是
`stdexec::inplace_stop_source`，都不能常量求值。所以能在 `static_assert` 里跑完的只有不碰它们的图——
只有一个汇点、没有入度 > 1 的节点、不会失败，例如一条 `noexcept` 的 `just | then` 链。`test/constexpr.cpp` 覆盖链与空图。

## 并发度

`test/concurrency.cpp` 测图的峰值并发：每个节点体进入时给"正在运行"计数加一并更新峰值；要验证并发的层，节点体在会合点
互相等待，人数到齐才离开——能同时跑就一定会被测到，被串行化则会合超时、判失败而不会卡死。每个节点进入时还检查前一层
已全部结束，保证并发不是靠违反依赖得来的。三种编译配置下结果一致：

| 形状 | 调度 | 理论峰值 | 实测 |
| --- | --- | --- | --- |
| 1 → 8 → 1 | 内联 | 1 | 1 |
| 1 → 8 → 1 | 线程池 | 8 | 8 |
| 1 → 4 → 8 → 1（相邻层全连接） | 线程池 | 8 | 8 |
| (1 → 4 → 8 → 1) + (x → y → z)，两张图作为节点组装进一张外层图 | 线程池 | 9 | 9 |

线程池用 `exec::static_thread_pool{16, bwos_params{.blockSize = 1}}`。默认的 `blockSize = 8` 下，worker 线程自己提交的任务
先进本地块，块写满才发布给别的线程窃取，于是"在 worker 上扇出"的后继全部滞留在这一个 worker 上。这是调度器的窃取粒度，
与图无关：测试里用纯 stdexec（`schedule | let_value(when_all(8 个任务))`，不经过 sdag）对照，默认参数下峰值同样是 1。

## 文件

```
include/sdag/                    （目录 = 概念分组，文件 = 模块；按依赖从底到顶，下层不认识上层）
  config.h          编译开关
  general/          通用，与图无关
    type_list.h         类型列表：type_list、at_t、index_of、concat、unique、transform、rebind，以及反射共用的 refl 基础
    sequence.h          编译期序列：to_array、sequence_t、for_each_value
    tuple.h             不可移动元组：indexed_tuple、按工厂原地构造、空元素 0 字节（empty / empty_for）
    digraph.h           有向无环图：shape<N>（邻接、入出度、拓扑序）、shape_error、可达关系 reach_v、roots_v / sinks_v / successors_v
    chains.h            偏序的最小链划分（二分图最大匹配）：哪些值可以依次共用一块存储
    relay.h             接力存储：一组类型轮流占用同一块内存
    cvref.h             copy_cvref_t
    signatures.h        完成签名：拆开（completion_trait）与拼装（make_completions_t）
    domain_merge.h      common_domain_t：一组 domain 合成哪一个
  stdexec/          对 stdexec 的适配：对其内部实现（双下划线名字）的全部依赖，见下文
    expressions.h       拆开 stdexec 的算法 sender
  node/             节点
    node.h              mode、CPO（id / dependencies / mode_of）、概念（key_value / node_spec_of / node_of）、basic_node、let / move / wait、make
    node_graph.h        节点规格 → 节点表（graph_table：每个依赖换成前驱下标 + mode）与有向无环图，以及键相关的错误诊断
  protocol/         节点与图之间的约定
    value_source.h      取值源协议：get_value_source 查询 + 签名接口 + 交付接口
    hole.h              前驱占位符 hole<I>，以及 hole_count_v
    node_env.h          节点看到的环境：取值源句柄 + 可选停止令牌 + 外层转发
    node_sender.h       把占位符喂给节点载荷得到节点 sender
  graph/            图
    description.h       图的描述：graph_data、graph_description（tuple-like，对算法标签泛型）、类型级查询与节点签名
    readers.h           谁读谁的值、谁 move 谁的值（move 给 V，let 给 V const&），move 用法的编译期检查所需的信息
    analysis.h          node_analysis：每个节点有值 / 错误 / 停止，各是什么类型
    storage_plan.h      storage_plan：哪些值要存、存时会不会抛、哪个节点直接转发（forward[J]）、值槽怎么复用（layout）
    pred_sigs.h         前驱的签名：取值源的签名接口
    visibility.h        node_view：节点实际看到的环境（要不要自带停止令牌）、在其中的分析与合法性
    slots.h             graph_state：值槽（按 layout 共用存储）与交付表，取值源的交付接口
    completions.h       整图的完成签名
    node_domain.h       节点的完成域与公共完成域
    attributes.h        图的属性：完成域与完成行为
  execution/        怎么执行
    fused.h             融合 op-state（默认连接）：状态机、调度与生命周期规则
  assembly/         拼装与入口
    graph_sender.h      图 sender = 描述 + 完成签名 + 属性 + 执行
    dag.h               入口：dag<K>、dag_t<K>、make_graph<Tag, K>
  sdag.h            总头（列出全部模块）
test/
  fixtures.h         测试共用的键、测试 sender / 接收者、mock domain
  test.cpp           编译期 + 运行期测试（各种形状、错误 / 停止传播、线程池、48 节点长链、16 路扇入、梯子、宽 when_all、生态接入）
  zero_overhead.cpp  分配计数与 op-state 大小
  concurrency.cpp    峰值并发
  constexpr.cpp      常量求值：不碰原子量与停止源的图在 static_assert 里跑完
```

### 两份编译期计算

`include/sdag/config.h` 自动探测：编译器定义了 `__cpp_impl_reflection` 且有 `<meta>` 时 `SDAG_REFLECTION = 1`，否则为 0；
也可以用 `-DSDAG_REFLECTION=0/1` 强制。CMake 选项 `SDAG_REFLECTION`（GCC 上默认 ON）只决定加不加 `-freflection`。

有两份编译期计算的文件（`grep -rln "#if SDAG_REFLECTION" include/sdag`：`general/` 的类型列表、序列、完成签名、domain 合并，`protocol/hole.h`，`graph/` 的 analysis / pred_sigs / node_domain），两种实现写在同一个文件里，
用 `#if SDAG_REFLECTION` / `#else` 隔开，对外名字完全相同；跨文件复用的反射函数放在各模块的 `refl` 命名空间，
其余实现细节放在 `detail`。两种模式生成的实体在不同的 inline namespace 里（`SDAG_ABI_NAMESPACE`），
同一个程序混用两种模式编译的翻译单元时链接期就会报错，而不是静默的 ODR 违规。

等所用的编译器都支持反射之后，删除模板版：

1. 这些文件删掉 `#else` 分支（`grep -rn "#if SDAG_REFLECTION" include/sdag`）；
2. `config.h` 里 `SDAG_REFLECTION` 为 0 的分支改成 `#error`；
3. CMake：`cmake/build_settings.cmake` 的 `SDAG_REFLECTION` 选项改为无条件 `-freflection`，`cmake/toolchain_guard.cmake` 按新的编译器下限调整，`CMakePresets.json` 删掉 `linux-gcc-templates-release`；
4. README 里两份实现的对照表。

两份实现的对应关系：

| 模板实现 | 反射实现 |
| --- | --- |
| `index_of` / `unique` / `concat` 递归偏特化 | 对 `std::vector<std::meta::info>` 的普通循环 |
| `completion_trait` 的偏特化 + 折叠 | `value_lists` / `error_types` / `sends_stopped(^^Completions)`：遍历 `template_arguments_of`，按 `return_type_of` 分类 |
| `valid_node_analysis<Self, Env, index_sequence<I...>>` 里的折叠表达式 | `analyze_nodes<Self, Env>()`：`template for` 按拓扑序逐节点取签名，填一个 `node_analysis_result<N>` 结构体 |
| `walk<Self, Step>` 基类链按拓扑序实例化，并累积合法性 / 第一个不合法的结果 | `template for` 按拓扑序实例化，累积进同一个结构体 |
| `sequence_of<Array>` 偏特化 | `substitute(^^index_sequence, reflect_constant(...)...)` |
| `for_each_value` 用 `index_sequence` 折叠 | `template for (constexpr auto value : Array)` |

运行期的东西（op-state 成员、`indexed_tuple`、交付表的一次性包展开）只有一份：它们不是"计算"。

反射特有的两个坑，代码里都绕开了：类模板的成员别名在类实例化时就会求值（所以不合法时 `completions` 给 `void`，
而不是让 `make_completions` 在 `^^void` 上抛异常）；consteval 计算没有编译器缓存（所以按拓扑序一次算进表里查，
不写"节点依赖前驱"的互递归——否则计算量随图里的路径数指数增长）。

## 构建

只依赖 stdexec 与 pthread；需要 Clang ≥ 22（Linux 上 clang++ + libc++，Windows 上 clang-cl + MSVC STL）或 GCC ≥ 16，C++26，Ninja。

| 预设 | 编译器 | 编译期计算 |
| --- | --- | --- |
| `linux-clang-debug` / `linux-clang-release` | clang++ + libc++ | 模板 |
| `linux-gcc-debug` / `linux-gcc-release` | g++ + libstdc++，`-freflection` | 反射 |
| `linux-gcc-templates-release` | g++，`SDAG_REFLECTION=OFF` | 模板（在 GCC 上也验证模板实现） |
| `windows-clang-cl-debug` / `windows-clang-cl-release` | clang-cl + MSVC STL | 模板 |

```bash
cmake --preset linux-clang-release
```

```bash
cmake --build --preset linux-clang-release && ctest --preset linux-clang-release
```

stdexec 的位置：`$NAGISA_LIBRARY_ROOT/stdexec`，或 `-DSDAG_STDEXEC_ROOT=...`；都没有时 FetchContent 拉取验证过的提交。
本库在 stdexec `8398d5a`（2026-10-20）、GCC 16.2.1、clang 22.1.8 上验证；Windows clang-cl 预设未在本机验证。

## 实现要点与边界

- **对 stdexec 内部实现的依赖只在 `stdexec/` 目录**（`expressions.h` 拆开 sender 表达式）。其余代码只用公开接口：sender / receiver 协议、`completion_signatures`、查询（`get_domain`、`get_completion_domain`、`get_stop_token`……）、`exec::completion_behavior` 与 `exec::get_completion_behavior_t`、`exec::throw_compile_time_error`。`stdexec/` 里（命名空间 `stdexec_adapter`）：
  - **读**：stdexec 的算法 sender 的 `[tag, data, child...]`（`stdexec_adapter::sender_expression`、`stdexec_adapter::parts_t`、`stdexec_adapter::apply`）。P2300 说 sender 表达式可以结构化绑定成这个形状，但 stdexec 的 `__tuple` 超过 8 个元素就不能结构化绑定（`when_all` 有 7 个子 sender 时就是这样），所以这里用它的内部别名 `__tag_t` / `__data_t` / `__children_t` 与 `__apply`。只认 stdexec 自己的算法 sender，自定义 sender 与 sdag 的图 sender 都被当作不透明的叶子。
  - 依赖型 sender 的报错用公开的 `exec::throw_compile_time_error<stdexec::dependent_sender_error, _WITH_SENDER_<S>>`（stdexec 自己的 `repeat_until` 也这样写）。
  - stdexec 升级、或者标准给出公开接口时只改这个目录。
  - 原来用到的 `__default_domain_like`、`__starting_domain_t`、`__completing_domain_t`、`__completion_domain_of_t`、`__common_domain_t`、`__get_completion_behavior`、`__copy_cvref_t`、`__get<I>`、`__sexpr_impl`、`__apply`、`__throw_dependent_sender_error` 都已换成公开接口或自己的实现。
- **编译期深度**。`then(f)` 问子 sender 的签名时，节点 N 的类型计算会嵌套进节点 N-1 的，长链会撞上编译器的模板深度上限。反射实现的 `analyze_nodes` 用 `template for`、模板实现的 `walk` 用基类链，都按拓扑序逐个取节点签名，前驱先实例化，后面的计算全部命中编译器缓存，嵌套深度变成常数。48 节点长链 + 16 路扇入 + 梯子的测试文件在本机编译约 25 s（g++）/ 45 s（clang） —— 这是实例化总量的代价，不是深度的。
- **GCC 不对"可能重叠的子对象"做复制消除**（CWG2403）：`[[no_unique_address]]` 的非空成员从纯右值初始化时，GCC 要求移动构造，而 operation state 不可移动（clang 照样消除）。所以 `indexed_tuple` 的非空元素只在 clang 上加 `no_unique_address`（`SDAG_OVERLAPPING_ELEMENT`），让后面的元素复用它的尾部填充；GCC 上只给空元素加。图嵌套图时整图 op-state 就是这样的非空元素。
- **图可以作为另一张图的节点**。内层图对外层来说是一个普通的 sender（控制依赖节点）：占位符的计数与替换不进入内层图（图 sender 不是 stdexec 的算法 sender，是不透明的叶子）；内层节点环境里的 `get_value_source` 遮蔽外层的，停止令牌逐层转发。
- **连接的值类别**：右值图 move 子 sender，左值（const 或非 const）图拷贝它们，对应 `connect() &&` 与 `connect() const&` 两个重载，完成签名也按这两种情况计算。
- **GCC 16 反射的几个坑**：模板实参位置的类型拼接要写 `typename [: :]`；`^^` 不能作用于 using 声明（`std::exception_ptr` 要经别名 + `dealias`）；`template for` 的范围必须是常量表达式（静态成员可以，consteval 函数里的 constexpr 局部变量不行）；`std::array<info, 0>` 没有可用的 `operator[]`（空图用 `info_array` 留一个占位元素）；noexcept / requires 里不能引用 `auto&&` 形参本身（`payload` CPO 以类型为参数）。
- **内联完成的栈深度**。节点在前驱的完成回调里被启动，一条全内联的链会递归 O(深度) 层；静态图的规模是编译期已知的常量，这与 `then` 链的行为一致。编译器对函数体的实例化也沿 `launch -> start -> ... -> node_done -> notify_edge -> launch` 这条路径嵌套，最长路径约 60 个节点以上会撞上 GCC 的模板深度上限（`-ftemplate-depth` 可以调）；拓扑序 `template for` 只解决了完成签名那一侧的深度。
- **let 的值在整图 op-state 里停留到整图完成**。给出的 `const&` 因此一直有效；这也是 `split` 做不到 0 开销而整图可以的原因。move 让用户显式决定提前结束一个值的生命周期，析构点在编译期确定，不需要引用计数。
- **错误发生后未启动的节点被跳过**，这是对 `when_all`"全部启动"语义的细化：跳过不改变结果（结果一定是错误 / 停止），只是不做无用功。
- 节点必须恰好完成一次；完成签名不能"撒谎"（`deliver` 里有 `static_assert` 对照接收者环境与整图环境下算出的占位符签名）。
- 节点适配器必须把占位符恰好用一次（构造时 `static_assert`）：前驱的值只交付一次，`repeat_n` / `retry` 这类会重启子 sender 的适配器不能套在输入上——这与把它们套在 `just(std::move(x))` 上会重复移动是同一回事。
- 外层停止回调只做一次状态切换和一次 `request_stop`，之后不再碰 op-state：`inplace_stop_source::request_stop` 自己处理"回调里把 source 销毁了"的情形，整图可能就在这次 `request_stop` 里同步完成。
- 诊断：重复键、未知依赖、自依赖、重复依赖、成环各是一条 `static_assert`；`shape_of<Nodes...>()` 不断言，返回 `shape_error`，便于测试。
