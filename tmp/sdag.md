在 @demo 下新建一个文件夹，是一个完全独立的项目
我需要一个模板库，依赖stdexec，std，实现类似以下效果：

```
using dag = xxx<pack<sndr1, fanin=<sndr2, sndr3>>, pack<sndr2, fanin=<sndr3>, ...>
static assert(sender<dag>)
```

大致意思是我需要一个静态的dag图，通过传入多个sender节点，并每个节点都声明自己依赖哪个任务，然后这个工具自动用::stdexec::when_all，::exec::split整理出dag图，并且整理出来后这个dag图也是一个sender，完成值为stdexec同样理念的设计
有以下几点要求：

1. 0开销
2. when_all，split这些扇入扇出操作可配置，并且尽可能的优化掉split(split会有动态内存分配，我们这种情况全是静态图，用不到split）
3. 要能够支持足够复杂的dag图，以及各种形状的dag图

/goal  你负责实现这个库，参考这个项目下的skill（编码，现代化要求），实现出来的库一定是0开销的，非0开销则是你实现的有问题

我先看一下项目里的 skill、记忆文件和现有 demo 的约定，再动手实现。

Diamond 图已经能跑通（输出 4）。接下来把组合策略、错误/停止传播、move-only 值、线程池并行这几类场景一起压一遍，再整理成正式测试与 CMake。

库已经完成并通过全部测试，新目录是 [demo/static-dag](demo/static-dag)（完全独立的 CMake 项目，未提交）。

## 用法

```cpp
auto g = ::sdag::dag(
	::sdag::make_node<a>(::stdexec::just(1)),
	::sdag::make_node<b, ::sdag::after<a>>(::stdexec::then([](int const& x) { return x + 1; })),
	::sdag::make_node<c, ::sdag::after<a>>(::stdexec::then([](int const& x) { return x * 2; })),
	::sdag::make_node<d, ::sdag::after<b, c>>(::stdexec::then([](int y, int z) { return y + z; })));
static_assert(::stdexec::sender<decltype(g)>);
```

你草图里的 `pack<sndr, fanin<...>>` 对应 `node<key, after<...>, payload>`，纯类型写法 `dag_sender<fused, node<...>...>` 同样满足 `sender`。节点载荷是 sender 就只有控制依赖，是适配器（`then(f)`、`continues_on(sched) | then(f)` 等）就按 `after<>` 的顺序收到前驱的值。完成语义与 `when_all` 同一套：汇点的值按声明顺序拼成 `set_value`，错误请求停止并跳过未启动的后继，错误优先于停止。

## 三条要求的落实

- **0 开销（默认 `fused` 策略）**：整图一个 op-state，节点 op-state 是它的直接成员。扇出不用 `split`，值存一份、后继按 `const&` 读，独占的值直接 move；扇入不用 `when_all`，入度 ≥ 2 才有一个原子倒计数。不会失败的图没有停止源、状态字和错误槽，被省掉的成员是真正的 0 字节。实测菱形图：0 次堆分配，op-state 112 字节恰好等于各部件之和；`-O2` 下三节点链折叠成常量。
- **扇入扇出可配置**：`composed<FanIn, FanOut>` 策略用传入的算法（默认 `when_all` + `split`）拼成普通 sender 树，作为语义参照与可配置入口。测试里两种策略互相对照。
- **复杂形状**：编译期做邻接矩阵、入出度、拓扑序分析；重复键、未知依赖、自依赖、重复依赖、成环各有一条 `static_assert`。测试覆盖菱形、多汇点、控制依赖、48 节点长链、1→16→1 扇出扇入、线程池上 200 次并行执行、错误与停止传播。

## 一个值得知道的实现点

`then(f)` 套到输入 sender 上时 stdexec 会立刻算一次完成签名，节点 N 的类型计算会嵌套进 N-1，48 节点长链直接撞 clang 的 1024 模板深度。`fused.h` 里的 `warm_up` 按拓扑序逐个预热节点签名，让后续计算命中编译器缓存，嵌套深度变成常数。含长链和扇入的测试文件编译约 40 秒，这是实例化总量的代价。

## 构建与验证

```bash
cmake --preset linux-clang-release && cmake --build --preset linux-clang-release && ctest --preset linux-clang-release
```

Debug 与 Release 两个预设下 `-Wall -Wextra -Wpedantic` 无警告，`sdag.test` 与 `sdag.zero_overhead` 全部通过。未能在本机验证：Windows clang-cl 预设。细节见 [demo/static-dag/README.md](demo/static-dag/README.md)。

A -> B -> C, A -> C这种场景测过吗

测过，但没有单独命名。它藏在两处里：

- `test_policies_agree` 的控制依赖图正是 `a → b → c, a → c`（`c` 声明为 `after<a, b>`，`b` 为 `after<a>`），两种策略都跑了。
- 编译期的拓扑序检查用的也是这个形状（`shape_of<n_c, n_a, n_b>`）。

为了把这个场景说清楚，我加一个独立的三角形测试，顺便验证值类别和启动顺序。

测过，现在也有了独立的三角形测试，Debug 与 Release 都通过。

**原本的覆盖**：`test_policies_agree` 里的控制依赖图就是 `a → b → c, a → c`（`c` 声明为 `after<a, b>`，`b` 为 `after<a>`），两种策略都跑；编译期拓扑序检查用的也是这个形状。

**新增的 `test_triangle`** 明确验证了这几点：
- 形状分析：`c` 入度 2，`a` 出度 2，`b` 出度 1，拓扑序 a, b, c。传递边 `a → c` 不会被去重，`c` 真的要等两个前驱。
- 运行期：`c` 只启动一次，且在 `a`、`b` 之后；结果为 `"a" + "ab"`。
- 值类别（fused）：`a` 有两个后继，`c` 收到的是 `std::string const&`；`b` 只有 `c` 一个后继，`c` 收到的是右值。

**顺带发现的一个差异**：composed 策略下 `c` 收到的 `a` 是右值，不是 `const&`。原因是 `when_all` 会把 `split` 交来的 `const&` 再拷贝一份存进自己的 op-state，再按右值交给后继。这是 `when_all` 自身的语义，不是 bug；fused 策略避免了这次拷贝，也是它比组合写法省的地方之一。测试里按策略分别断言，并写了注释。

增加一条约束，保持0开销，并且复用stdexec的组建，因为when_all，then，这些是属于标准化组建，会被domain识别并优化，如果你自己从头造，这些东西都吃不到优化
/goal 0开销为硬前提，在不损失当前性能的前提下，接入stdexec生态

你的接入stdexec生态的实现思路是什么

全部完成：两个预设构建与测试通过（42 + 3 项检查），复查无必修项。下面回答你的问题。

## 接入 stdexec 生态的思路

**出发点**：domain 的定制只认 `tag_of_t` + `transform_sender`，而且只作用于"由 sexpr 组成的算法树"。上一版的融合 op-state 是手写 sender，domain 看不见它；对 nvexec 这类 domain 来说，节点各自连接到宿主端的接收者，每个节点都得退回宿主同步一次——`then` 吃到了定制，扇入扇出没有。

**但我没有把融合路径改成用 `when_all` 拼**，原因是实测过的：`when_all` 会把共享值再拷贝一份，`split` 必须堆分配，这都违反 0 开销的硬前提。于是分成四层：

1. **图本身变成一个标准算法节点。** `dag(nodes...)` 返回 `::stdexec::__sexpr<dag_t, dag_data<键表, 依赖表>, 节点 sender...>`。子 sender 就是用户写的 `then(f)`、`continues_on(sched) | then(f)` 表达式原样保留，所以 `tag_of_t`、`exec::sender_for<G, dag_t>`、子节点内省、domain 对 `dag_t` 的定制全都成立；节点连接时照常被所在 domain 变换。

2. **节点的输入做成"依赖环境"的叶子 sender。** 节点 sender 要在 `dag()` 构造时就建好，但它的输入类型（前驱的值）那时还不知道整图。`input_sender<after<...>>` 像 `read_env` 一样，从接收者环境里的转发查询 `get_graph_state` 拿到整图状态类型，再算前驱的值签名。这让节点 sender 既是普通 sexpr，又在无环境时正确地表现为 dependent sender。

3. **属性按 `when_all` 的同一套语义回答。** `get_env(g)` 报告"图在汇点完成的地方完成"（汇点的公共完成域）以及完成行为，这样 `dag | continues_on(...)` 这类后续算法和 `set_value_t` 阶段的 domain 推导都拿得到正确的域。

4. **只在 domain 真的会定制 `when_all` 时才降解。** `dag_t::transform_sender` 按两个阶段的 domain（`start_t` 看 `get_domain(env)`，`set_value_t` 看图的完成域）做一次类型探针：把汇点里的输入叶子换成签名一致的占位 sender，拼成 `when_all<汇点...>`，用 `__default_domain_like` 问这个 domain 会不会改写它。会，就把整图降解成 `when_all` / `split` / `let_value` 组合交给它（叶子替换成真正的扇入）；不会（默认 domain、只定制 `bulk` 的线程池 domain），就走原来的融合 op-state。默认路径的运行时结构一字未动。

**验证方式**：一个只定制 `when_all` 的 mock domain 证明降解发生、定制被触发、结果一致；线程池 domain 证明不降解仍走融合；完成域与 `continues_on(pool)` 的汇点一致。nvexec 本机没有 CUDA，没有实测。

## 这轮顺带的改进

- 唯一汇点的值不再存槽位，直接转发（菱形 op-state 从 112 降到 104 字节）；共享按"读值的后继数"而非出度判定。
- 修掉了审查发现的外层停止回调与多汇点计数的竞态（这个写法是从 `when_all` 照搬的，那里看起来有同样的窗口）。
- 不可停止令牌时省掉回调槽；MSVC ABI 的 `no_unique_address` / `empty_bases` 宏；输入叶子恰好用一次的 `static_assert`。

改动未提交，详见 [demo/static-dag/README.md](demo/static-dag/README.md)。

用反射优化计算表达，避免大量模板计算，加上宏区分，如果支持反射的话就优先用反射计算
（用反射的好处是代码更利于人阅读，和维护）

编译器不限制，如果g++16支持的更好，那就用g++16，尽可能的用全量的反射

README 已更新。剩下只等审查 agent 对反射改写的结论，有问题就按清单修。

审查结论：0 开销保持（新旧版 `-O3` 反汇编逐行相同），但有两处回归必须修——节点签名不合法时 `analysis::completions` 被类实例化提前求值变成硬错误；降解路径的 consteval 互递归没有缓存，节点多时指数爆炸。另外把 `deliver_impl`/`emit_values` 的递归改成"consteval 排好交付表 + 一次包展开"，清掉死代码。开始修。

Debug/Release 全部通过（43 + 3 项），README 已补上反射特有的两个坑与路径深度上限。现在只等复查 agent 对这批修改的核对。

复查结果：只剩一个真问题——用户的错误类型若恰好是 `std::monostate`，`error_variant` 会出现重复类型，按类型访问就硬错误；改成按下标访问。另外删两处死代码，补两条测试。

全部完成，两个预设（g++ 16 Debug/Release）44 + 3 项检查通过，两轮审查提出的问题都已修掉。

## 这轮做了什么

**工具链**：本机 clang 22 完全没有反射（无 `-freflection`、无 `<meta>`），GCC 16.2 的 `-freflection` 实现了 P2996 + 展开语句，stdexec 也能用 g++ 编译。所以按你的决定改成只支持 GCC ≥ 16（CMake 守卫、`-freflection`、`linux-gcc-debug/release` 预设），不保留模板回退分支——同一套编译期计算写两遍正好违背"利于阅读和维护"的初衷。这条已记进记忆。

**反射化的范围**（编译期计算全部换掉，运行期结构原样保留）：

| 原来 | 现在 |
| --- | --- |
| 类型列表的递归偏特化（`index_of`、`unique`、`concat`…） | 对 `std::vector<std::meta::info>` 的普通循环 |
| 签名分类偏特化 + 折叠 | `summarize(^^Completions)` 遍历 `template_arguments_of`、按 `return_type_of` 分类 |
| `analysis_impl<Self, index_sequence<I...>>` 的十几个折叠 | `analyze()`：`template for` 按拓扑序逐节点取签名，填一个 `analysis_result<N>` |
| `warm_up` 基类链 | 不需要了，`template for` 本身按拓扑序实例化（审查实测：100 节点逆序声明的链旧版超深度，新版能编；80 节点链编译 120 s → 63 s） |
| `substituted` 偏特化重建 sexpr | 递归 consteval 函数 + `substitute(^^__sexpr, {...})` |
| `notify_edge` 折叠、`launch_roots` | `template for (constexpr size_t to : shape.successors(J))` |
| `deliver_impl` / `emit_values` 的累积参数包递归 | consteval 铺平一张交付表，再一次包展开 |
| composed 的互递归偏特化 | 按拓扑序一次算进 `lowered_tables` 查表 |

**0 开销验证**：审查者把新旧版在同一编译器下 `-O3` 反汇编逐行比对，`zero_overhead.cpp` 零差异，`test.cpp` 函数数相同；反射结果（`info`、`vector`）没有任何泄漏到运行期。GCC `-O2` 下 DAG 链 30 条指令，手写管线 29 条。

**审查发现并修掉的回归**：类成员别名会在类实例化时求值，节点签名不合法时 `make_completions` 在 `^^void` 上抛异常变成硬错误（改为惰性）；consteval 计算没有编译器缓存，降解类型的互递归随路径数指数增长（改为查表，8 层梯子测试已加入）；`std::monostate` 作为错误类型会与 variant 占位重复（改为按下标访问）。

**GCC 16 反射的坑**（都记在 README 与记忆里）：模板实参位置要写 `typename [: :]`；`^^` 不能作用于 using 声明；`template for` 的范围必须是常量表达式；`std::array<info, 0>` 不可用；noexcept/requires 里不能引用 `auto&&` 形参。

**遗留**：降解路径（composed）在 12 层以上的梯子编译时间增长变快，审查者判断是 stdexec 的 `split`/`when_all` 嵌套类型变大，融合路径是线性的；函数体实例化深度沿最长路径约 60 节点会撞 `-ftemplate-depth`，是原有限制。改动未提交。

1. 加个#if reflection，如果编译器支持反射，那么就用反射编译，否则就按老模板计算。主要原因是我用的编译器clang23暂时还不支持反射，可能会有问题，后续等支持了我再一键删除。
2. 补上并发度测试，要求在1→8→1 内联, 1→8→1 线程池, 1→4→8→1线程池，更复杂的(1 -> n -> m -> 1) + (x -> y -> z) （多个dag图被组装成一个dag图），这时候的峰值并发也要达到理论最大
3. 保持对stdexec的__*系列接口的干净，__*并不是标准接口，并不稳定，我们的库要保持对__*的依赖足够干净，方便后续维护替换
4. 

4. 

4.  
  

4. 
4. __*系列替换不了的话可以不替换，但是要保持接口干净方便以后替换

基础头写好了。接下来是编译期计算层的第一部分（类型列表、完成签名、sender 表达式遍历），反射版与模板版各一份，对外接口完全相同。

计算层两份都写完了。现在写共享的 `graph.h`：节点环境、整图状态、输入 sender、属性、降解判据和 `dag_t`。这里只用计算层接口和 stdexec 公开接口。

`attrs` 里错误 / 停止通道的完成域写错了一处，补上 `compute::common_domain_t` 接口后修正。

接下来改写 `fused.h`：运行期逻辑不变，只是把反射专用写法换成计算层接口（`for_each_value` 等），图 sender 的子节点经它自己的 `get<I>` 取出。

现在改写 `composed.h`：降解后的类型取自计算层的 `lowered` 表；运行期重建 sender 表达式只经桥接层的 `apply` / `make_expression`。

核心头文件都改完了。先用最小的 `zero_overhead.cpp` 在三种配置（g++ 反射、g++ 模板、clang 模板）下编译，逐个排错。

三种配置下 44 项功能测试全部通过。检查 `__*` 是否真的只剩桥接层：

整个测试 70 秒，几乎全耗在默认参数对照里（7 次串行超时，每次 10 秒）。给会合点加可配置的超时，对照只用 100 毫秒。

差异里混着 `std::string` 之类的函数，那是反射模式多包含了 `<meta>` 带进来的标准库代码。只比较 sdag 自己的函数：

审查在后台跑。先更新 README：

README 更新完毕。再把记忆里"sdag 只用 GCC 反射"那条改成现状（双实现、clang 23 是用户的主力编译器、BWOS 结论）。

README 与记忆都已更新。代码和测试已在五个 Linux 预设下全部通过，现在只等审查结论；若有问题会按清单修完再汇报。

审查结论：三个目标都达成；0 开销在 g++ 下布局逐字不变。有一个必须修的回归，外加几处可改进：

1. **[高] 子 sender 多于 6 个的 stdexec sender 放进节点会硬错误**：stdexec 的 `__tuple` 超过 8 个元素就不能结构化绑定了。修法：桥接层对 stdexec 的 sender 表达式改用它自带的内省（仍然只在桥接层），同时把"sender 表达式"收窄到真正的 stdexec 表达式，顺带修掉"任意可绑定的自定义 sender 被误当成表达式"的问题。
2. **[中] 构造图 sender 时每个子 sender 多一次移动**（`std::tuple` 先物化再移动）：改用 `indexed_tuple` 的工厂构造。
3. **[中/低] clang/libc++ 下值槽失去尾部填充复用**：`no_unique_address` 按编译器区分，clang 全加，GCC 只给空类型。
4. 其余小项：标签识别失效时 `static_assert`；公共 domain 对 `indeterminate_domain<>` 的吸收与 stdexec 对齐；`connect` 收敛为 `&&` / `const&` 两个重载；两种模式放进不同的 inline namespace，混用时链接期就报错；死代码；外层图的依赖也插桩检查。

先确认 `get_completion_domain<>`（`void` 版本）是否公开可用：

`get_completion_domain<>` 是公开的，`phase_domain` 改用它，与 stdexec 自己的规则完全一致。开始按清单修。

只剩一条注释里提到内建名字。全部五个 Linux 预设重新构建并测试：

README 已更新。五个预设全部通过，现在只等这轮修复的复查结论。

复查结论：五项修改全部正确。唯一的建议是外层依赖检测（gate）灵敏度不够——外层 `start` 节点太快，提前启动的内层根可能漏报。在 `start` 里先等 5 毫秒再置位，让违规必然被抓到。

三项要求都已完成。五个 Linux 预设下三个测试全部通过：g++ 反射版、clang 模板版、g++ 强制模板版，各自 Debug 和 Release。改动未提交，只有 `signatures.h` 的删除已在暂存区。

**1. 反射 / 模板双实现**
- 编译器支持反射时用反射计算，否则用模板计算。由 `SDAG_REFLECTION` 宏切换，它在 [config.h](demo/static-dag/include/sdag/config.h) 里自动探测，也能用 `-D` 强制指定。clang 23 会自动走模板版。
- 只有编译期计算层分成两份，在 [compute/](demo/static-dag/include/sdag/compute) 下，接口一致。运行期代码只有一份。
- 同一个 g++ 下，两种模式中 sdag 相关的 `-O3` 机器码逐行相同，切换不影响运行期。
- 删除模板版：删掉 `compute/templates/` 目录，再去掉两个选择头里的 `#else`。README 里列了配套的 CMake 与预设清理项。
- 两种模式放在不同的 inline namespace 里，混用两种模式编译的文件会在链接期报错。

**2. 并发度测试**

| 形状 | 理论峰值 | 实测 |
| --- | --- | --- |
| 1→8→1，内联 | 1 | 1 |
| 1→8→1，线程池 | 8 | 8（降解路径也是 8） |
| 1→4→8→1，线程池 | 8 | 8（降解路径也是 8） |
| (1→4→8→1) + (x→y→z)，两张图作为节点组装进一张外层图 | 9 | 9 |

- 测法：同一层的节点在会合点互等，人数到齐才离开，所以测到的就是实际能同时跑的数量。被串行化时会合超时，测试判失败而不会卡死。每个节点进入时还检查依赖，外层图的依赖也检查了。
- 线程池必须用 `bwos_params{.blockSize = 1}`。默认参数下，worker 线程自己提交的任务不会被别的线程窃取。测试里附了一段不经过 sdag 的纯 stdexec 对照，默认参数下峰值同样是 1。这是调度器的性质，与图无关。

**3. `__*` 接口收口**
- 对 stdexec 内部名字的依赖全部集中在 [stdexec_bridge.h](demo/static-dag/include/sdag/stdexec_bridge.h)。真正没有公开替代的只有两处：降解时重建 sender 表达式，以及依赖型 sender 的报错。
- 读取 stdexec sender 内部结构这一项也留在桥接层，用的是 stdexec 的内部别名。原因是 stdexec 的 sender 超过 8 个元素就不能结构化绑定。
- 图 sender 换成了 sdag 自己的类型，stdexec 照样能识别它的标签。其余原来用到的 `__*` 都换成了公开接口：`get_domain`、`get_completion_domain<>`、`exec::completion_behavior`、`when_all(...)`。

**审查中发现并修掉的问题**
- 子 sender 多于 6 个的 `when_all` 放进节点会编译失败，已修，并补了测试。
- 构造图 sender 时每个子 sender 多一次移动，已修。现在构造 3 次移动、连接 5 次，都不多于上一版。
- 图作为另一张图的节点时，GCC 下编译失败。原因是 GCC 不对"可能重叠的子对象"做复制消除，已绕开。顺带让 clang 下的 op-state 复用尾部填充，一个三节点图从 72 字节降到 64 字节。

Windows clang-cl 预设本机无法验证。

用户补充了要求：注释要写好，难读的地方要把思路写清楚。我先读一遍几个核心文件，把乱的地方列出来，再一起改。

`graph_traits.h` 写好了：图的类型级查询从 `graph_sender.h` 里拆出来，计算层不再依赖图 sender 的定义。接下来 `graph_sender.h` 只留类型本身与 tuple-like 协议。

接下来写 `lowering.h`：原 `composed.h` 里的降解实现，加上 `lower` 入口；`composed` 策略挪到 `dag.h`，与 `fused` 策略、`dag()` 放在一起。

Now the fused op-state — the hardest file to read. I'll rewrite it with a header that explains the scheduling algorithm and the lifetime rules, and group the members by role.

Now `dag.h` becomes the public entry: `dag_t` (graph construction + transform_sender), the `fused`/`composed` policies, and `dag()`. Then delete `graph.h` / `composed.h`.

Now the remaining shared headers: `config.h`, `meta.h`, `node.h`, `shape.h`, `stdexec_bridge.h`. Let me reread them and tighten comments where the reasoning isn't stated.

三种模式下全部通过。确认这次整理没有改变任何生成代码——与整理前的目标文件逐指令比较：

代码整理完了。五个 Linux 预设下三个测试全部通过，改动未提交。

**文件结构**

原来的 `graph.h` 有 600 多行，混着节点环境、输入 sender、整图状态、属性、降解判据、`dag_t` 六件事。现在按职责拆开，从底到顶依次是：

| 文件 | 内容 |
| --- | --- |
| [graph_traits.h](demo/static-dag/include/sdag/graph_traits.h) | 图的类型级查询：第 I 个节点是谁、依赖谁、谁读它的值 |
| [graph_sender.h](demo/static-dag/include/sdag/graph_sender.h) | 只放图 sender 的类型与 tuple-like 协议 |
| [node_input.h](demo/static-dag/include/sdag/node_input.h) | 节点怎么拿到前驱的值：输入 sender、节点环境、整图状态 |
| [attributes.h](demo/static-dag/include/sdag/attributes.h) | 完成签名、属性、降解判据 |
| [fused.h](demo/static-dag/include/sdag/fused.h) | 融合 op-state |
| [lowering.h](demo/static-dag/include/sdag/lowering.h) | 降解（原 `composed.h`）与 `lower()` |
| [dag.h](demo/static-dag/include/sdag/dag.h) | 公开入口：`dag_t`、两种策略、`dag()` |

- 编译期计算层原来依赖 `graph_sender.h`，现在只依赖 `graph_traits.h`，层次是单向的。
- 打断头文件环的前向声明集中到 [fwd.h](demo/static-dag/include/sdag/fwd.h)，文件开头写了为什么会有环。

**补写了思路的地方**

- **[fused.h](demo/static-dag/include/sdag/fused.h) 开头**：op-state 的三层继承布局，以及为什么节点接收者只指向中间一层。还写了调度算法（根启动、节点结束、计数归零、跳过），以及"完成之后不能再碰 this"的生命周期规则。
- **[node_input.h](demo/static-dag/include/sdag/node_input.h) 开头**：节点怎么拿到前驱的值。输入 sender 本身不存值，靠环境查询找到整图状态，所以多个后继能共享 `const&`，而不需要 `split`。
- **停止源的"鸡生蛋"问题**：要不要停止源取决于节点会不会失败，而节点的签名又取决于它看到的环境。解法是先在探测环境下算一遍。
- **交付表**：为什么先在编译期铺平成一张表，再做一次包展开。
- **[attributes.h](demo/static-dag/include/sdag/attributes.h) 开头**：降解判据怎么用一个 `when_all` 探针去问 domain"会不会改写"。
- **计算层**：两份实现各自说明为什么按拓扑序计算（避免签名计算层层嵌套撞上深度上限），以及反射版特有的两个坑。
- **[meta.h](demo/static-dag/include/sdag/meta.h)**：为什么不用 `std::tuple`，`leaf` 的三种写法分别对应什么情况。

**没有改变行为**

我把整理前后的目标文件逐指令比较了一遍：

- `zero_overhead` 测试里 sdag 和测试本身的函数，在反射、模板、clang 三种配置下逐指令相同。
- `concurrency` 测试的差异只有两类，都不涉及逻辑：
  - 反射版：命名空间从 `composed_detail` 改名为 `lowering_detail`，符号名随之变化，指令数相同。
  - 模板版：一个降解路径的测试函数里，GCC 换了几个寄存器的分配。