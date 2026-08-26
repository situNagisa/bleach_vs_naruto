# dynamic-dag

运行期构图、多次执行的 DAG 调度器，写成一个 **stdexec 自定义 sender 算法**。

这个 demo 服务于之前那场讨论里的一个具体分支：**图在运行期构好一次，然后反复执行**（case B），
**只有控制依赖**（数据走旁路，不走边）。目标是"0 开销的动态图"——
Stroustrup 意义上的 0：手写这套调度所需的最小成本，不多一分。

## 结论先行

| 主张 | 实测 |
| --- | --- |
| 每次执行 0 次堆分配 | ✅ 稳态恒为 0（前若干次的非零来自 stdexec，见下） |
| 每条边 1 次 `fetch_sub` | ✅ 设计如此，无 refcount / `shared_ptr` / `std::function` |
| 大图不爆栈 | ✅ 200,000 节点长链全量内联跳过，13 ms，无递归 |
| 失败不死锁 | ✅ 失败节点仍然给后继减计数（带毒标记） |
| 调度损耗 | ✅ 墙钟 / 关键路径下界 = **1.01** |

## 为什么不是 `split` + `when_all`

扇出用 `split`、扇入用 `when_all`，是教科书答案，也是能跑的答案。但把它俩拆开看：

- `split` = 堆上的共享状态 + 引用计数 + 结果 variant + 原子侵入式等待链表 —— "跑一次然后广播"的闩锁。
- `when_all` = 内联的子 op-state + 原子倒计数 + 一个 `inplace_stop_source` —— "倒计数器"。

一旦把节点摊平成数组、边只表达先后，**广播和倒计数就退化成同一样东西：每个节点一个原子计数器**。
组合现成 combinator 的代价是每节点约 4 次分配、每条边约 2 次原子操作；直接写成一个 sender 算法则是
每节点 0 次分配、每边 1 次 `fetch_sub`。

支点在 `dag.h` 里：`node_receiver` 是一个**固定类型**，所以
`connect_result_t<const Sender&, node_receiver>` 在 `add_node<Sender>` 模板里就能算出来 ——
op-state 的大小和对齐在构图期已知，于是一个 arena 能装下全图所有节点的 op-state。

DAG 不是树，而结构化并发要求树形所有权。捅穿这个洞只有两条路：`split` 的按共享点引用计数，
或者把所有权提到**整图**这一层（arena + 索引）。标准只提供前者，是因为它无法假定存在一个清晰的
图边界；case B 恰好有。

## 三个坑

1. **绝不在 op-state 自己的完成回调里销毁它自己** —— 推迟到整图拆解时统一销毁。
2. **内联完成会造成 O(深度) 的栈递归** —— 用一个 `thread_local` 蹦床把它摊平成循环。
   场景 4(b) 就是专门打这一点的：20 万节点长链、链首失败、其余全部**内联**跳过。
3. **失败也必须给后继减计数**（带毒标记），否则 `outstanding_` 永远到不了 0 → 死锁。

内存序：毒标记 `relaxed` 存，计数器 `fetch_sub(acq_rel)`。读到 0 的那个线程已经 acquire 了
所有前驱的 release，因此既看得到它们写的数据，也看得到它们写的毒标记。

## 两个测出来的坑（都不在图层）

### 1. `blockSize=8` 会让整张图退化成串行

`exec::static_thread_pool` 用 BWOS（block-wise ordered work stealing）：worker 从**自己线程**
提交的任务先进本地块，**块写满才发布**出去供别人窃取，默认 `blockSize = 8`。

而 DAG 的扇出恰恰是"节点完成后，在完成它的那个 worker 上提交 2~3 个后继"——
永远填不满一个块，于是后继全滞留在该 worker 的私有队列里被它自己顺序跑完，其余 worker 一直睡着。

纯 stdexec 对照实验（不含本 demo 任何代码，4 个各 3.6 ms 的任务）：

| | 线程数 | 墙钟 |
| --- | --- | --- |
| 池**外**提交 x4 | 4 | 3.88 ms |
| 池**内**提交 x4，`blockSize=8`（默认） | **1** | **14.76 ms** |
| 池**内**提交 x4，`blockSize=1` | 4 | 3.68 ms |

所以 `main.cpp` 里 `bwos_params{.numBlocks = 32, .blockSize = 1}` 不是调参，是这个模型的硬性前提。
任何"在完成线程上扇出后继"的调度器都会撞上，与图层实现无关。

### 2. 前若干次执行的 2 次分配

`schedule_start -> vtable->start -> static_thread_pool` 入队时，stdexec 在某个 worker 线程
**首次向池提交**时才惰性建它的 remote/BWOS 队列（48 字节 + 1280 字节对齐块）。
每线程一次，与图无关也与帧数无关；20 个 worker 全被触达之后恒为 0。

这条是 backtrace 抓出来的。它的教训是：**观察窗口必须长过线程数**，否则会把一次性的每线程成本
误读成每帧开销 —— 这个 demo 最初就是这么误判的。

## 关于并行度 1.40

场景 1 那张渲染图很窄：关键路径的实测耗时是 15.10 ms，忙碌时间合计 21.40 ms，
所以并行度的**理论上限**就是 21.40 / 15.10 ≈ 1.42。实测 1.40，墙钟只比下界高 1%。
数字小不是调度器差，是图本身没有更多可并行的东西 —— 所以 demo 把这个下界也一并打出来，
否则读者无从判断。

同理，甘特图画的是节点体**真实占用 CPU** 的区间，不是 tracer 的 `started` 事件。
`started` 打在 `vtable->start`（提交时刻），三个 start 时间重合只证明"同时入队"，什么也不证明。

## 构建

只依赖 stdexec 和 pthread。

```bash
cmake -S demo/dynamic-dag -B build/dynamic-dag -G Ninja -DCMAKE_BUILD_TYPE=Release -DDYNAMIC_DAG_STDEXEC_ROOT=/path/to/stdexec
```

```bash
cmake --build build/dynamic-dag && ./build/dynamic-dag/dynamic-dag
```

不带 `-D` 时默认取 `$NAGISA_LIBRARY_ROOT/stdexec`。在主工程里则由根 `CMakeLists.txt` 的
`BVN_BUILD_DEMOS` 带出来，走 `STDEXEC::stdexec`。

单文件编译也行：

```bash
g++ -std=c++23 -O2 -Wall -Wextra -I /path/to/stdexec/include demo/dynamic-dag/main.cpp -o dagdemo -pthread
```

> GCC 16 + CMake 的 C++23 模块扫描（`-fmodules-ts`）在扫 stdexec 头时会 ICE，
> 所以目标上设了 `CXX_SCAN_FOR_MODULES OFF`。

## 文件

- `dag.h` —— 调度核心。`graph`（构图 + storage 池）、`graph_sender` / `graph_op`（对外是普通 sender）、
  `node_receiver` + vtable（类型擦除的支点）、蹦床、arena。
- `main.cpp` —— 四个场景，外加一个替换全局 `operator new` 的分配计数器。

## 边界

- 只做控制依赖。数据依赖需要在 arena 里再开一层带类型的槽位，或退回到节点自取的旁路（demo 用的是后者，
  `frame_context`）。
- 图必须在 `add_node` 时给出前驱，且前驱索引小于自身 —— 所以**无环是构造性保证**，
  节点索引天然就是拓扑序，不需要排序也不需要环检测。
- 这是 CPU 侧的 DAG。GPU 侧的依赖是另一张图；senders 在渲染里的真正价值是一个
  `vk_scheduler`，让 op-state 的完成挂在 timeline semaphore 上。
