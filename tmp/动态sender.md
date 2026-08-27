跟我探讨探讨，c++26的execution中，动态的sender模型应该怎么做？ 面对的问题是，多个动态任务互相依赖成一个dag图

1. 如何理清面对的问题是什么
2. 如何用execution解决这个问题

先把结论摆前面：**C++26 的 sender 模型天生是"结构化"的，而动态 DAG 天生是"非结构化"的。硬把动态 DAG 塞进 sender 组合式（`when_all | then | split`）里能做，但你会发现自己实际上在重新实现一个计数器调度器，只是外面裹了一层 sender 皮。所以关键是先想清楚"动态"到底动态在哪一层"，再决定 execution 参与到哪一层。**

---

## 一、先把问题拆开：你说的"动态"是哪一种？

我觉得这一步比写代码重要得多。至少有三个正交的轴：

### 轴 1：图的形状什么时候确定

|类别|描述|难度|
|---|---|---|
|**A. 编译期已知**|`when_all(a,b) \| then(f)` 直接写出来|不是你的问题|
|**B. 运行期构图、构完再跑**|每帧/每次请求构一次图，然后整体执行|**绝大多数所谓"动态 DAG"是这个**|
|**C. 执行中长出新节点**|节点跑完才知道后继是谁（递归分解、爬虫、推测执行）|真正的动态，需要 `async_scope`|

B 和 C 的解法完全不同。B 因为构图和执行分离，**你可以在构图阶段做拓扑排序**，剩下的就只是"按拓扑序发射"，动态性被压缩掉了一大半。C 才必须面对"结构化并发被打破"的问题。

### 轴 2：依赖是控制依赖还是数据依赖

- **纯控制依赖**：B 在 A 之后跑，A 的结果通过共享上下文（blackboard / 资源句柄 / arena 索引）传递。
- **数据依赖**：B 的参数就是 A 的返回值，类型各不相同。

这个区别的影响是决定性的。纯控制依赖时，每个节点都可以擦除成同一个类型：

```cpp
using node_sender = any_sender_of<set_value_t(),
                                  set_error_t(std::exception_ptr),
                                  set_stopped_t()>;
```

**没有值类型要擦除**，`when_all` 的结果也永远是 `void`，整套东西瞬间简单一个数量级。而数据依赖你要么把值擦成 `std::any`/`variant`（失去类型安全 + 每条边一次分配），要么维护一个"编译期已知的类型全集"然后 variant 分发。

我的经验是：**大部分真实场景（渲染图、构建系统、资源加载流水线）本质是控制依赖 + 副作用**，把它伪装成数据流反而是自找麻烦。先确认你是不是真需要数据依赖。

### 轴 3：规模与复用

- 节点数 10 还是 100k？前者随便怎么写，后者"每节点一次堆分配 + 一次类型擦除虚调用"就要认真算了。
- 同一个形状的图是跑一次还是跑一万次（比如每帧一次）？如果形状稳定，**构图和执行要分离**，把拓扑排序/资源别名分析的结果缓存下来，每帧只做 execute。

### 轴 4：这些节点是不是同构的执行资源

CPU 线程池、GPU 队列、IO、网络的完成信号机制完全不同。sender 的价值恰恰在这里（`starts_on` / `continues_on` 跨 scheduler），但也意味着你需要为每种资源写 scheduler。

**先回答这四个问题，方案基本就定了。**

---

## 二、用 execution 怎么做

### 方案 B1：把 DAG 折叠成 sender 表达式（"纯正"做法）

DAG = 扇入 + 扇出。execution 里正好各有一个原语：

- **扇入** = `when_all`
- **扇出** = `split`（sender 默认是 single-shot、单消费者的，出度 > 1 必须 `split` 共享结果）

按拓扑序遍历，每个节点：

```cpp
std::vector<node_sender> out(n);
for (int i : topo_order) {
    out[i] = when_all_range(gather(out, preds[i]))   // 扇入
           | continues_on(sched_of(i))
           | then([&, i]{ nodes[i].run(ctx); })
           | split;                                   // 扇出：多消费者共享
}
auto graph = when_all_range(gather(out, sinks));
std::this_thread::sync_wait(std::move(graph));
```

这个写法很漂亮——它字面上**就是**那张图。但有几个坑：

1. **标准里的 `when_all` 是变参的，没有 range 版本。** 你必须自己写一个 `when_all_range(std::vector<node_sender>)`。写起来不难：一个原子计数器 + 一个 `inplace_stop_source` + 一个 op-state vector。但注意——**这个计数器就是方案 A 的计数器**。这是这个方案最诚实的一点：你并没有绕开 dataflow 调度器，只是把它封装成了一个 combinator。 （千万别用 pairwise 折叠 `when_all(when_all(a,b),c)...` 代替，op-state 嵌套深度会变成 O(n)，编译期和栈都会爆。）
    
2. **`split` 不是免费的。** 每个出度 > 1 的节点都有一个堆分配的共享状态 + 引用计数 + 完成时遍历等待者链表。出度为 1 的节点不要加 `split`。
    
3. **op-state 生命周期。** 静态组合时整张图的 op-state 是一个巨大的嵌套对象，一次 `connect` 全部构造好。加了 `split` 之后嵌套被打断（每个 split 自己堆分配），这反而是好事。
    
4. **取消语义。** `when_all` 自带"任一失败则请求取消其余"。整张图共享一个 `inplace_stop_source`，通过 env 注入，取消是统一的——这是 execution 相比手写调度器实打实的收益。
    

### 方案 A：拓扑序 + 前驱计数器（"务实"做法）

完全不建 sender 图。每个节点一个原子计数器：

```cpp
struct node {
    node_sender work;
    std::atomic<int> pending;      // 未完成前驱数
    std::vector<int> successors;
};

void on_done(int i) {
    for (int s : nodes[i].successors)
        if (--nodes[s].pending == 0) spawn(s);
}
```

execution 的角色退化为**单个节点的执行封装**：`scope.spawn(starts_on(sched_of(i), nodes[i].work | then([i]{ on_done(i); })))`，整图完成 = `sync_wait(scope.join())`。

- 每节点一次分配，无嵌套擦除，cache 友好，节点数大时明显更好。
- Taskflow、UE 的 TaskGraph、各家 render graph 实际上都是这么干的。
- 代价：你放弃了组合性，自己写调度器和取消传播。

### 方案 C：真·运行期长出的图 → `async_scope`

如果节点执行中会 spawn 新节点，结构化并发就被打破了。补救方式是**用一个 scope 把整次运行重新围起来**（P3149 的 `counting_scope`，注意它不在 C++26 基线里，得用 stdexec 或自己实现）：

```cpp
counting_scope scope;
scope.spawn(root_task(scope));       // 任务内部可以继续 scope.spawn(...)
sync_wait(scope.join());             // 结构在这里被恢复
```

这里 `scope` 就是整张动态图的所有权边界，`join()` 是唯一的汇合点。

---

## 三、我会怎么选

**混合方案，边界划在"节点"上：**

1. **只在节点边界做类型擦除，且只擦控制流**（`any_sender_of<set_value_t(), set_error_t(...), set_stopped_t()>`）。数据走旁路的 typed context / arena，不要擦值类型。
2. **形状确定就用 A（计数器），形状可能中途变就用 C（scope）**。B1 那套 `when_all_range + split` 我会写出来当作 API 门面，但内部实现就是 A 的计数器——两者不矛盾。
3. **构图与执行严格分离**：`build()` 产出拓扑序 + 前驱计数模板 + 资源别名信息并缓存；`execute()` 每次只 memcpy 一份计数器然后发射。形状稳定的场景（每帧）这个收益最大。
4. **每次运行一个 `inplace_stop_source`**，通过 env 下发，让取消和错误传播统一走 execution 的机制。这是 sender 模型最值得留下的部分。

---

## 四、一个针对你这个仓库的提醒

看到 `vkfu`，如果这个 DAG 是 render graph 的话，有个坑值得提前说：**CPU 侧的 DAG 和 GPU 侧的 DAG 不是同一张图，千万别让前者去镜像后者。**

- GPU 的依赖是 barrier / timeline semaphore 表达的，**在提交时才生效**；
- CPU 侧录制 command buffer 其实近乎"完全并行"（只要资源状态分析已经在构图阶段做完），是一张几乎扁平的图。

所以真实结构通常是：`build + cull + 资源别名分析（纯数据、单线程）` → `并行录制（embarrassingly parallel，用不到 DAG）` → `按拓扑序提交 + barrier（串行）`。硬套一个 async DAG 到 CPU 侧，收益可能远小于复杂度。

execution 在这里真正有价值的地方是另一处：**写一个 `vk_scheduler`，它的 op-state 完成信号挂在 timeline semaphore 上**（一个 poll 线程或 `VK_KHR_external_fence` + epoll）。这样 GPU 完成就变成了一个正常的 sender completion，可以和 CPU 任务在同一个 `when_all` 里组合。这才是 sender 模型给渲染带来的独特收益。

---

想继续深入的话，告诉我上面**轴 1（B 还是 C）**和**轴 2（控制依赖还是数据依赖）**你落在哪，我可以把对应的 `when_all_range` 或 counting-scope 调度器实际写出来。

轴：

1. B，C的情况也有，但是现在主要先讨论B
2. 控制依赖，数据依赖其实我也想支持，但是既然复杂度高的话就先搁置
3. 这个我觉得不应该是个问题，我想做的是0开销的动态图，既然0开销那么就跟规模没有关系，应该任何情况都要考虑到
4. 这个问题我没看懂，节点是否是同构的执行单元对他们有什么影响吗，执行单元不是归starts_on这些sender管吗

详细解释下你的方案B1，我没看懂

先回答你的两个疑问，再展开 B1 —— 因为这两点直接决定 B1 最后长成什么样。

## 0. 两个澄清

### 关于"0 开销"

字面意义的零开销做不到：动态图意味着**至少**每节点一次间接调用（形状不在类型里，就必须有一次运行期分发）和每条边一次原子操作（并发的前驱汇合，信息论下限）。

但 Stroustrup 意义的零开销是可达的，把目标定成这个：

- **每次图执行 0 次堆分配**（整图一块 arena，形状稳定时跨帧复用 → 摊销到 0）
- **每条边恰好 1 次 `fetch_sub`**，没有多余的原子
- **每节点恰好 1 次间接调用**，没有 `shared_ptr` 引用计数、没有 `std::function`、没有 `virtual` 继承链
- 手写一个专用调度器也不可能更快

这个目标是能达到的，而且**恰恰是达到它的过程会告诉你 B1 的正确形态**。下面就是这条路。

### 关于轴 4

你说得对，"在哪跑"确实归 `starts_on`/`continues_on` 管。我问的不是这个，是**完成信号从哪个线程、以什么方式回来**，它影响的是存储策略：

|               | CPU 线程池节点                     | GPU / IO 节点                               |
| ------------- | ----------------------------- | ----------------------------------------- |
| 完成时机          | `start()` 返回前后不久，同一个 worker 上 | `start()` 立刻返回，几十 ms 后由 poll 线程/reaper 回调 |
| op-state 生命周期 | 短，可紧凑复用                       | 必须跨越挂起点存活                                 |
| 后继启动位置        | 可以内联在当前 worker 上继续（cache 热）   | 必然跨线程，且可能需要 hop 回去                        |

两个具体后果：

1. **op-state 必须活到 completion 为止**，而异步节点的 completion 可能在很久以后 → 你不能用"栈式/递归复用"的存储方案，必须整图预留（下面 arena 的设计前提）。
2. 如果节点的 work 本身是异步的（返回 sender 而不是值），`then` 不够，要 `let_value`。这决定了节点擦除接口的形状。

所以它不改变你用哪些 sender 算法，它改变的是**存储和重入假设** —— 而零开销设计恰恰全押在这两件事上。

---

## 1. B1 是什么：从静态情形推

先看编译期图，把机制看清楚。链式的情况平凡：

```cpp
auto s = just() | then(A) | then(B);   // A → B
```

有**扇出**就出问题了。`A → B`，`A → C`：

```cpp
auto a = just() | then(A);
auto b = a | then(B);     // ✗
auto c = a | then(C);     // ✗ a 被消费两次
```

sender 默认是 **single-shot、单消费者**的：`connect` 要吃掉它。`a` 不能被两个下游同时连。这就是 `split` 存在的理由：

```cpp
auto a = (just() | then(A)) | ex::split;   // 变成可复制、多消费者
auto b = a | then(B);
auto c = a | then(C);
auto d = ex::when_all(b, c) | then(D);     // 扇入
sync_wait(d);
```

**DAG = `split`（扇出）+ `when_all`（扇入）。** B1 就是把这个模式在运行期按拓扑序批量套用。

---

## 2. 这两个原语内部到底是什么（关键）

搞清楚这个，后面的结论就是自明的。

### `split` 的机械原理

`split(s)` 分配一块共享状态，里面有：

```
shared_state {
    refcount
    connect_result_t<S, split_receiver>   // 内部 op-state
    variant<完成结果>                       // 结果存这里，广播给所有人
    atomic<waiter*> waiters                // 侵入式等待者链表
}
```

- 每次 `connect(split_sender, R)` 只产生一个很小的 op-state：`{ shared_state*, R, waiter* next }`。
- `start()`：CAS 把自己压进 `waiters` 链表。**第一个压入者**顺便 `start` 内部 op-state。
- 内部完成时：结果存进 variant，把 `waiters` 原子换成"已完成"哨兵，然后遍历链表逐个 complete。
- 之后再 `start` 的，看到哨兵直接内联完成。

**说白了就是一个"跑一次 + 广播"的闩锁。**

### `when_all` 的机械原理

`when_all(s1...sn)` 的 op-state 里有：

```
{ child_op_1 ... child_op_n,      // 内联，不分配
  atomic<size_t> count = n,
  inplace_stop_source stop,
  storage for each child's values,
  atomic<disposition> }           // value / error / stopped
```

- `start()` 挨个 `start` 所有 child。
- 每个 child 完成时 `count.fetch_sub(1)`，**最后一个**（`== 1`）负责向下游发射。
- 任一 child 出错 → 记下 disposition，`stop.request_stop()` 取消其余。

**说白了就是一个原子倒数计数器。**

> 这里就有个很重要的观察，先记下：**扇出是"广播闩锁"，扇入是"倒数计数器"。这两件事合起来，等价于"每个节点一个前驱计数器，前驱完成时减 1，减到 0 就启动"。** 后面会用到。

---

## 3. 朴素的 B1 长什么样

控制依赖，所以完成签名全图统一，没有值类型要擦：

```cpp
namespace ex = std::execution;

using node_sender = any_sender_of<          // 标准里没有，stdexec 有 / 自己写
    ex::set_value_t(),
    ex::set_error_t(std::exception_ptr),
    ex::set_stopped_t()>;
```

先写一个 range 版的 `when_all`（标准的 `when_all` 是变参的，没有 range 版；**绝对不要用 pairwise 折叠代替**，`when_all(when_all(a,b),c)...` 会让 op-state 嵌套深度变成 O(n)，编译期和栈都会炸）：

```cpp
// 结构 = 上面 when_all 的机制，只是 child_op 存在 vector 里
struct when_all_range_op {
    std::vector<child_op> children;        // ← 一次分配
    std::atomic<size_t>   count;
    std::inplace_stop_source stop;
    void start() noexcept { for (auto& c : children) ex::start(c); }
};
```

然后按拓扑序折叠：

```cpp
std::vector<node_sender> out(n);
for (uint32_t i : topo_order) {
    out[i] = when_all_range(pick(out, preds[i]))     // 扇入
           | ex::continues_on(sched_of(i))
           | ex::then([&, i] { nodes[i].run(ctx); })
           | ex::split;                              // 扇出
}
sync_wait(when_all_range(pick(out, sinks)));
```

它字面上就是那张图，很漂亮。**但它离零开销很远。**

---

## 4. 算账

每个节点的开销：

|项|次数|说明|
|---|---|---|
|`any_sender` 擦除|1 次堆分配|装 sender 本体|
|`any_sender` 的 op-state|1 次堆分配|大小运行期才知道|
|`split` 共享状态|1 次堆分配 + 引用计数原子|出度 > 1 时必须|
|`when_all_range` 的 `vector<child_op>`|1 次堆分配|每个有前驱的节点一次|
|`split` 的等待者链表 CAS|每条边 1 次|压链表|
|`when_all` 的 `fetch_sub`|每条边 1 次|计数|

**≈ 4 次分配/节点，≈ 2 次原子/边**，外加 `split` 的 refcount。10 万节点就是 40 万次 malloc。

而且注意，`pick(out, preds[i])` 里那些 `node_sender` 是被 `split` 共享的副本，每次复制都是一次 refcount 原子。

**这不是"实现不够好"，是这个组合方式本身的结构性成本**：通用组合子必须假设"我不知道我被组合成什么形状"，于是每个节点都得自己管自己的生命周期和存储。

---

## 5. 零开销版本：不要组合现成算法，写一个专用 sender 算法

关键在于，**你处在 B 情形（构图和执行分离）**，所以在构图完成的那一刻你已经掌握了全部信息：

- 节点总数、每个节点的前驱数、扁平化的后继表
- **每个节点 op-state 的精确尺寸和对齐** —— 因为 `add_node<S>` 是模板，`S` 在那一刻还在手上

第三条是所有优化的支点。

### 5.1 布局

```cpp
// 节点接收器：固定类型，所以 connect_result_t<S, node_receiver> 在 add_node 处可求
struct node_receiver {
    graph_op_base* g;
    uint32_t       idx;
    void set_value() && noexcept        { g->on_node_done(idx); }
    void set_error(std::exception_ptr e) && noexcept { g->on_node_error(idx, std::move(e)); }
    void set_stopped() && noexcept      { g->on_node_stopped(idx); }
    graph_env get_env() const noexcept; // 暴露 stop_token / scheduler / allocator
};

struct node_vtable {                    // 每个 S 一份，static constexpr，无分配
    void (*construct)(void* op, void* sndr, graph_op_base*, uint32_t) noexcept;
    void (*start)(void* op) noexcept;
    void (*destroy)(void* op) noexcept;
};

struct node_desc {
    const node_vtable* vt;
    uint32_t sender_off, op_off;        // arena 内偏移
    uint32_t pred_count;                // 计数器初值
    uint32_t succ_begin, succ_end;      // 指向扁平 succ_ 数组
};
```

### 5.2 构图期：只是累加偏移

```cpp
template <ex::sender S>
uint32_t graph::add_node(S&& s, std::span<const uint32_t> preds) {
    using op_t = ex::connect_result_t<S, node_receiver>;
    static constexpr node_vtable vt = make_vtable<S, op_t>();   // 静态存储

    uint32_t soff = bump(sizeof(S),    alignof(S));
    uint32_t ooff = bump(sizeof(op_t), alignof(op_t));          // ← 支点在这
    // 记录 preds → 反向填 succ_，pred_count = preds.size()
    ...
}
```

`arena_size_` 就是所有 sender + 所有 op-state 的字节和。构图结束后 `graph` 完全不可变。

### 5.3 执行期

`graph` 本身暴露成一个**普通 sender**，于是整张图能被 `when_all`、被 `sync_wait`、被外层取消：

```cpp
struct graph_sender {
    const graph* g;
    using completion_signatures = node_sigs;
    template <class R> graph_op<R> connect(R r) const;   // 分配 arena
};
```

`graph_op<R>::start()`：

```cpp
void start() noexcept {
    std::memcpy(counters_, g->pred_template_, n * sizeof(uint32_t));  // ← 复用！
    outstanding_.store(n, relaxed);
    for (uint32_t i = 0; i < n; ++i)                    // 全部就地构造 op-state
        g->nodes_[i].vt->construct(op_at(i), sndr_at(i), this, i);
    for (uint32_t r : g->roots_) start_node(r);
}
```

完成回调：

```cpp
void on_node_done(uint32_t i) noexcept {
    for (uint32_t s : g->succ_of(i))
        if (counters_[s].fetch_sub(1, std::memory_order_acq_rel) == 1)
            start_node(s);
    if (outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        finish();                                       // 向外层 receiver 发射
}
```

**`split` 和 `when_all` 都消失了。** 因为第 2 节说过：广播闩锁 + 倒数计数器，在"节点已经被扁平化成数组"之后，就退化成 `counters_[s]` 这一个原子。扇出不需要广播（没有值要广播，控制依赖！），扇入不需要独立的 `when_all` op-state（计数器就在目标节点身上）。

### 5.4 最终账单

|项|次数|
|---|---|
|堆分配|**每次执行 1 次**（arena）；跨帧复用 arena → **0**|
|原子操作|每条边 1 次 `fetch_sub` + 每节点 1 次 `outstanding`|
|间接调用|每节点 1 次（`vt->start`）|
|引用计数|**0**|
|`vector` / `std::function` / `shared_ptr`|**0**|

每条边一次 `fetch_sub` 是任何并发 dataflow 调度器的下限。**这就是零开销。**

---

## 6. 所以 B1 的真正结论

你可能已经发现了：**在 B 情形 + 控制依赖下，把 B1 推到零开销，它收敛成了方案 A。**

我上一轮说"B1 内部其实就是 A 的计数器"，就是这个意思。但这不代表 B1 白讨论了，收获是**知道边界划在哪**：

- **图这一层不要用现成组合子** —— 通用组合子为了不知道形状而付的代价，正是你已知形状后不该付的。图本身要写成**一个自定义 sender 算法**。
- **节点这一层照常用 sender 生态** —— `then` / `let_value` / `starts_on` / `continues_on` / 自定义 scheduler。节点内部想多复杂都行，对图透明。
- **`graph_sender` 对外仍是一个合规 sender** —— 能被组合、能被取消、能 `sync_wait`。结构化并发在图的边界上被恢复了。

一句话：**execution 给你的是"节点是什么"的统一抽象和取消/错误语义，图的拓扑调度是你自己的事。**

---

## 7. 三个必踩的坑

**① 不要在 completion 里析构自己的 op-state。**

```cpp
n.vt->destroy(op_at(i));    // ✗ 此刻还在这个 op-state 的调用栈里
for (uint32_t s : succ) ...
```

必须等整图结束统一析构。arena 反正已经预留了，没损失。

**② 内联完成会导致 O(n) 栈递归。** `on_node_done → start_node → set_value → on_node_done → ...`，如果节点用的是 inline scheduler 或工作已完成，一条长链会把栈打穿。要么强制节点在 scheduler 上完成，要么在 `start_node` 里做 trampoline：内联启动**一个**后继，其余推给 scheduler。

**③ 失败传播不做会死锁。** 这是手写版本最常见的 bug： 节点 `i` 出错 → 它的后继计数器永远减不到 0 → `outstanding_` 永远减不到 0 → 整图挂死。 必须**沿 DAG 向下毒化**：出错时把 `i` 的可达后继全部标记 skipped 并逐个把 `outstanding_` 减掉（可以复用同一个计数器遍历，代价与正常路径同阶）。同时 `stop_.request_stop()` 取消已在跑的兄弟节点，用一次 `compare_exchange` 记住第一个 error。

---

顺带一提：**op-state arena 的尺寸优化，和你渲染图里的 transient resource aliasing 是同一个问题**——按拓扑序做活跃区间分析，不相交的 op-state 可以复用同一块字节。如果你已经有一套别名分析，可以直接套过来，arena 从 `sum(sizeof)` 降到 `max_antichain(sizeof)`。不过这属于第二步优化，第一版直接 sum 就好。

要不要我把 `graph` + `graph_sender` + `graph_op` 这一套真的写出来？大概三百行，包含上面三个坑的处理。

不着急确定方案，目前只是为了把问题探讨明确

好，那就继续把问题面铺开。我觉得目前最有价值的动作是**重新框定问题**，因为我们前两轮其实是在一个被过早收窄的问题上做优化。

---

## 一、把"动态 DAG"拆成三个被混在一起的子问题

我认为这是最关键的一次拆分：

|子问题|内容|execution 管吗|
|---|---|---|
|**拓扑表达**|谁依赖谁；依赖怎么被声明出来；如何保证无环、无遗漏|**几乎完全不管**|
|**执行调度**|何时启动、在哪个资源上跑、怎么取消、错误怎么传|**强项，核心价值**|
|**数据绑定**|节点跑的时候看到什么状态；上游产出如何到达下游|管一部分（值传递），但方式很受限|

sender/receiver 是一套**调度与完成语义**的抽象。它对"拓扑"的表达能力，仅限于"能在类型里写死的那种拓扑"。

而人们说"动态 DAG 难"的时候，痛点分布大概是：拓扑 40%、数据绑定 40%、调度 20%。**也就是说 execution 覆盖的恰好是最不难的那部分。** 这不是说它没用——统一的取消/错误/scheduler 抽象很值钱——但如果指望"用 execution 解决动态 DAG"，方向本身就偏了。

值得先确认：**你的痛点主要落在哪一格？** 如果是拓扑或数据绑定，那 execution 只能是配角。

---

## 二、逼问一下：动态性到底从哪来

"动态"这个词太便宜了。我见过的来源有四种，只有最后一种是真的：

1. **数据驱动配置** —— 图由配置文件/脚本/编辑器定义。 → 拓扑在"加载时"确定，之后长期不变。这是**加载期动态**，跑起来完全静态。可以在加载时把图编译成一个扁平结构，甚至生成代码。
    
2. **并行度依赖数据** —— "对 N 个物体各跑一次"，N 运行期才知道。 → 这**不是 DAG 问题，是 bulk 问题**。`bulk` 就是干这个的。别把它建模成 N 个节点。
    
3. **条件启用** —— 某个 pass 开/关、某条分支走不走。 → 这是**静态图 + 节点谓词**。拓扑是全集固定的，运行期只是把部分节点标记为 no-op（或者做一次剪枝）。比真动态便宜一个数量级。
    
4. **执行中发现** —— 跑完才知道后继是谁。 → 这才是真动态，也就是我们说的 C。
    

现实系统里 1+2+3 常常占了 90%，但因为它们混在一起，人就会得出"我需要一个通用动态 DAG 引擎"的结论，然后为 10% 的场景给 90% 的场景交税。

**值得盘一下：你手上的具体任务，按这四类分，比例是多少？**

---

## 三、把上一轮关掉的两个开关重新打开，各自会破坏什么

我们上一轮得到的干净结论，前提是"B + 纯控制依赖"。这两个假设各自撤掉的代价不对称，值得单独看：

### 撤掉"纯控制依赖"（加入数据依赖）

破坏的东西：

- **节点不再同构。** 现在每个节点的完成签名不同，`connect_result_t` 也不同。类型擦除从"擦一个 `void()` 签名"变成"擦任意值类型"。
- **边有了存储。** 控制依赖的边是 0 字节（就是计数器减 1）。数据依赖的边要存值、要管值的生命周期（多个消费者共享一个值 → 这时 `split` 的广播语义**真的需要**了，控制依赖时它退化没了）。
- **构图期要做类型检查。** 运行期连边时怎么保证 `A` 的输出类型能喂给 `B`？要么运行期 type-id 检查（有开销、错误延迟到运行期），要么在构图 API 上用模板保住类型（但那样图就不能从配置文件构了，回到第二节的矛盾）。

**但有一条逃生通道值得认真考虑**：把数据依赖降级成"控制依赖 + 显式命名的存储槽"。也就是节点不返回值，而是写进一个 typed blackboard 的某个 slot，下游从 slot 读。边依然是 0 字节，类型安全靠 slot 句柄（`slot<T>`）在构图期保住。代价是"值的生命周期"从自动变手动。

这个通道我觉得是值得的——**渲染图、构建系统、编译器 pass 管线，全都是这么干的**，因为它们的"数据"本来就是共享的大对象（纹理、buffer、AST），不适合按值在边上流动。反倒是"每条边携带一个值"的纯 dataflow 模型，只适合小值、树形消费的场景。

**问题：你的数据依赖是"小值、点对点"还是"大对象、共享"？** 这个答案基本决定了要不要真做数据依赖。

### 撤掉"B"（加入 C：执行中长节点）

破坏的东西更彻底：

- **arena 尺寸不再可预知。** 零开销方案的支点就是"构图期知道所有 op-state 尺寸"。C 情形下节点是跑出来的，只能退回按需分配（或分段 arena / 池）。
- **拓扑序不存在。** 不能预计算 `pred_count`，不能预计算关键路径，不能做资源别名分析。
- **"图完成"的判定变了。** B 里是 `outstanding` 减到 0；C 里"减到 0"不代表结束（可能有节点还没被生出来）。需要 scope 的 join 语义：**未完成计数 + "不再有新增"的显式承诺**。这是 `async_scope` 存在的全部理由。
- **取消的语义变了。** B 里取消是"通知所有已存在节点"；C 里还要拒绝新的 spawn。

**但是**：B 和 C 可以嵌套共存，而且我认为这是最健康的形态——**C 只出现在节点内部**。某个节点内部动态展开一堆子任务，用一个局部 scope 围起来，对外仍然是一个"完成/失败"的单点。整张大图仍然是 B。

只有当"新长出的节点需要参与全局拓扑"（比如它要成为另一个已存在节点的前驱）时，才是真的 C。**这种需求你有吗？** 如果没有，C 根本不需要在图层解决。

---

## 四、还没进入视野、但可能比上面更要命的维度

我们前两轮完全没谈的：

### 1. 节点粒度 —— 这是对"零开销"最有力的质疑

零开销是相对什么基线说的？我上一轮给的定义是"相对手写调度器"。但还有个更狠的基线：**相对于不建图**。

如果节点平均耗时 200ns，而调度一次要 1 次原子 + 1 次间接调用 + 一次可能的跨核唤醒（几百 ns 到几 μs），那么**图的开销超过了工作本身**。这时唯一正确的优化是**构图期做节点合并**：把线性链上的、同 scheduler 的、无扇出的相邻节点熔合成一个节点。

这是编译器思路：图是 IR，构图期应该有优化 pass（死节点消除、链熔合、常量折叠式的静态节点内联）。**这可能比 op-state arena 的字节数重要十倍。**

**问题：你预期的节点粒度是多少？节点数量级是多少？** 这两个数决定了整个设计的重心在哪。

### 2. 数据绑定的生命周期 —— 构图/执行分离的真正难点

"构图一次、执行多次"听起来很美，但节点的 work 是构图期创建的 sender，它捕获了什么？

- 如果捕获了本次执行的数据（帧数据、请求上下文），那图**不能复用**，每次都得重建 → 前面所有的预计算全白做。
- 如果要复用，节点必须捕获**稳定的间接层**（`ctx*` + slot 索引），执行时才解析。

这就逼出一个设计：**节点的 work 不能是 sender 本身，而应该是"给定 context 产出 sender 的工厂"**。但工厂又意味着每次执行要重新构造 op-state（尺寸倒是仍然已知，因为工厂返回类型固定）。

我觉得这是整个设计里最容易翻车、也最少被讨论的地方。**拓扑复用是容易的，数据绑定的复用才是难的。**

### 3. 关键路径与优先级

`counter == 0 就 spawn` 是无优先级的贪心调度。但 DAG 有关键路径，先跑关键路径上的节点能显著缩短 makespan。构图期算一次每个节点的"到汇点的最长路径"作为静态优先级几乎免费——**但这要求 work-stealing 队列支持优先级**，而大多数不支持。这是图层和 scheduler 层的接口张力：图知道优先级，scheduler 不接受优先级。

### 4. 资源约束 —— DAG 模型的表达力缺口

纯 DAG 只能表达"先后"，不能表达"这两个节点不能同时跑"（独占 GPU queue、内存预算上限、同一个 command pool）。真实系统里这类约束很多，而它们**不是依赖边**——加成依赖边会过度串行化。

这是 DAG 调度和真实调度之间的根本差距，通常要在调度器里加一层资源令牌。值得提前想，因为它会侵入节点的接口。

### 5. 确定性与可重放

并发调度的 bug 极难复现。一个务实的要求是：**支持"单线程串行模式"，按某个确定的拓扑序跑完整张图**，用来定位问题。这个要求会反过来约束设计——比如节点不能假设自己在特定线程上、不能依赖并发时序。早期定下来很便宜，后期加很贵。

### 6. 可观测性

每个节点的开始/结束打点、关键路径可视化、等待时间归因。这个必须在**节点启动/完成的那两个位置**埋钩子，也就是我们零开销方案里 `start_node` 和 `on_node_done` 的位置。开销上要能编译期关掉。

---

## 五、一个绕不过去的矛盾

值得点明：**DAG 不是树，而结构化并发的全部前提是"生命周期嵌套成树"。**

sender 模型的所有优雅之处——op-state 在栈上、无分配、无引用计数、`connect` 返回的对象拥有一切——都来自树形所有权。DAG 有菱形（`A→B, A→C, {B,C}→D`），菱形不是树。

所以**任何动态 DAG 方案都必然要在树形所有权上打一个洞**。区别只在于打洞的方式：

- `split` 的打法：**引用计数**。每个共享点自己管自己，局部、通用，但每个洞都要交税。
- arena 的打法：**把所有权整体上提**。所有权从"每个节点"提到"整张图"，图这一层是树形的（`graph_op` 拥有一切），图内部退化成裸指针+索引。一个洞，一次税。

这两种打法之外我想不出第三种。**而选哪个，取决于"图"这个边界是否清晰稳定**——B 情形下它非常清晰（一次执行 = 一个 scope），所以 arena 打法压倒性地好；C 情形下边界模糊，refcount/scope 才有价值。

这也解释了为什么标准库只给了 `split`：它不能假设你有清晰的图边界。**而你有。这是你能做得比标准组合子好的全部理由。**

---

## 六、方案对不对，要能回答这些问题

先立验收标准，方案自然会被筛出来：

- **死锁自由**：任何节点失败/取消，整图一定在有限时间内完成。（第一大坑）
- **错误不丢**：多个节点同时失败，至少一个 error 被完整报出，其余不泄漏（`exception_ptr` 不能悄悄丢）。
- **取消及时**：外层 stop 请求下发后，未启动的节点一定不再启动，已启动的收到 stop token。
- **内存序正确**：节点 `i` 的所有写，对其后继可见。（靠边上那次 `fetch_sub` 的 `acq_rel` + 读取侧 acquire 建立 happens-before，这条要能被明确论证，不能靠"应该没问题"）
- **重入安全**：completion 内联发生时不爆栈、不重入死锁。
- **可复用性**：同一张图跑 N 次，第 2 次开始 0 分配。
- **可降级**：能切换到单线程确定性模式。

---

## 七、我想反过来问你的几个问题

1. 痛点主要在**拓扑表达 / 数据绑定 / 执行调度**的哪一格？
2. 第二节那四类"动态性"，你的场景比例大概是？
3. 节点的**粒度和数量级**？（决定要不要做构图期熔合，也决定"零开销"的基线该怎么定）
4. 数据依赖是**小值点对点**还是**大对象共享**？
5. 有没有"新长出的节点要成为已存在节点的前驱"这种真 C 需求？
6. 有没有**资源互斥**类的非依赖约束？

	我觉得 3 和 4 是最能改变结论的两个。

如何理解你的代码，我没看懂

先把 `dag.h` 完整读一遍，我按它实际的样子讲，不凭记忆。

`dag.h` 只有一个核心想法，其余全是为了把它塞进 sender 模型付的税。我按"先算法、后税"的顺序讲。

---

## 一、先把 execution 完全忘掉

整个调度算法就是这些，跟 C++ 无关：

```
每个节点有一个计数器 counter[i] = 前驱个数
每个节点有一个后继列表 successors[i]

启动：所有 counter == 0 的节点（根）
某个节点跑完时：
    for 每个后继 s:
        if (--counter[s] == 0) 启动 s
    if (--outstanding == 0) 整图完成
```

**扇出**（一个节点有多个后继）= 上面那个 `for` 循环。 **扇入**（一个节点有多个前驱）= `--counter[s] == 0` 这个判断。

这就是我在讨论里说的"`split` 和 `when_all` 退化成同一个东西"。`split` 是"广播给 N 个等待者"，`when_all` 是"等 N 个人到齐"——摊平成数组之后，它们是同一个 `counter[]` 的两个方向。代码里就是 [dag.h:678](app://localhost/epitaxy/demo/dynamic-dag/dag.h:678) 的 `retire()`，十几行。

**如果你只想看懂一个函数，看 `retire()`。剩下 800 行都是为了让这十几行能用在 sender 上。**

---

## 二、唯一的难点：op-state 放在哪

sender 模型有三条硬约束：

1. `connect(sender, receiver)` 返回一个 **op-state**，它**不可移动**（P2300 规定）。
2. op-state 的**类型**由 `(Sender, Receiver)` 这一对决定。
3. 每个节点的 `Sender` 类型都不一样（`then(schedule(sch), lambda1)` 和 `then(schedule(sch), lambda2)` 是两个不同类型）。

所以：**N 个节点有 N 个互不相同、且不可移动的 op-state 类型**。它们没法放进同一个 `vector`。

常规解法是每个节点一次堆分配：

```cpp
std::vector<std::unique_ptr<void, void(*)(void*)>> ops;  // 每节点 1 次 malloc
```

我的解法是：**让 Receiver 固定**，于是 op-state 的**尺寸和对齐**在 `add_node` 那一刻就能算出来，把它们累加成一个 arena 里的 offset。

看 [dag.h:374](app://localhost/epitaxy/demo/dynamic-dag/dag.h:374) 那几行：

```cpp
using operation_type = detail::node_operation_t<Sender>;
//                   = connect_result_t<const Sender&, node_receiver>
//                                                    ^^^^^^^^^^^^^ 固定类型！

operation_arena_size_ = 对齐向上取整(operation_arena_size_, alignof(operation_type));
desc.operation_offset = operation_arena_size_;      // 记下这个节点的 offset
operation_arena_size_ += sizeof(operation_type);    // 往后推
```

`add_node` 是模板，`Sender` 在这里还是完整类型，所以 `sizeof` / `alignof` 立刻可求。等 `add_node` 返回，`Sender` 这个类型就永远消失了——**但尺寸已经被记进 `operation_offset` 这个 `uint32_t` 了**。

这就是我说的"支点"。8 个节点算下来 arena 是 832 字节，一次 `make_unique<byte[]>` 全装下。

---

## 三、这是为什么必须有 `graph_run` / `graph_op` 两层

这一点最容易卡住，单独说。

`node_receiver` 里存的是 `graph_run*`（[dag.h:102](app://localhost/epitaxy/demo/dynamic-dag/dag.h:102)），不是 `graph_op<Receiver>*`。为什么？

- 外层的 `Receiver` 是在 `sync_wait(graph.run())` 那一刻才知道的。
- 但 arena 的尺寸必须在 `add_node` 那一刻就算好。
- 如果 `node_receiver` 依赖外层 `Receiver`，那 `connect_result_t` 也依赖它，arena 尺寸就**算不出来**了。

所以必须有一个**与外层 Receiver 无关的类型**给节点回调打，那就是 `graph_run`。`graph_op<Receiver>` 继承它，只在最后一步（`complete()`，[dag.h:798](app://localhost/epitaxy/demo/dynamic-dag/dag.h:798)）用一个虚函数把结果交给真正的外层 receiver。

```
node_receiver  ──持有──>  graph_run          （无模板，节点只认识这个）
                              ▲
                              │ 继承
                     graph_op<Receiver>      （有模板，只负责最后一跳）
```

**整份代码里唯一的虚函数就是这一个**，每次执行调用一次。

---

## 四、总共只有 7 个数据结构

分两组，这是理解全局的骨架：

**构图期，`graph` 持有，只算一次：**

|成员|是什么|
|---|---|
|`nodes_`|每节点一条：`{vtable*, sender*, operation_offset, predecessor_count, name}`|
|`sender_arena_`|所有 sender 的副本住在这里（分块，所以永不移动）|
|`successor_offsets_` + `successors_`|后继表，CSR 格式（下面解释）|
|`roots_` / `depth_`|根节点列表 / 每节点的最长路径深度|

**执行期，`run_storage`，池化跨次复用：**

|成员|是什么|
|---|---|
|`operation_block`|一整块字节，所有 op-state 按 `operation_offset` 住在里面|
|`counters[]`|每节点一个 `atomic<uint32_t>`，就是第一节那个 `counter[]`|
|`poisoned[]`|每节点一个 `atomic<bool>`，"你的前驱失败了，别跑"|

内存布局长这样：

```
operation_block:  [ op0 ][ op1 ][  op2  ][ op3 ]...
                    ↑      ↑       ↑
nodes_[0].operation_offset=0
       [1].operation_offset=96
       [2].operation_offset=192
```

**CSR 是什么**：把边表压成两个数组，避免 `vector<vector<>>` 的 N 次分配。

```
successor_offsets_ = [0, 3, 4, 6, 7, ...]
successors_        = [shadow, gbuffer, ui, ssao, lighting, ...]
                      └───── cull 的后继 ─────┘└ gbuffer 的 ┘
```

节点 `i` 的后继就是 `successors_[offsets_[i] .. offsets_[i+1])`。[dag.h:422](app://localhost/epitaxy/demo/dynamic-dag/dag.h:422) 的 `finalize()` 就在干这件事（计数 → 前缀和 → 填充，标准三步 CSR 构建）。

---

## 五、vtable 为什么正好是 4 个函数

`Sender` 类型在 `add_node` 之后就没了，但有 4 件事必须在之后还能对它做。所以 [dag.h:115](app://localhost/epitaxy/demo/dynamic-dag/dag.h:115) 存了 4 个函数指针，每个 `Sender` 类型一份 `static constexpr`（**零分配，在 .rodata 里**）：

|函数|何时调用|干什么|
|---|---|---|
|`connect`|每次执行开始，`launch()`|在 arena 的 offset 处就地构造 op-state|
|`start`|该节点计数器归零时|`stdexec::start(op)`|
|`destroy_operation`|整图结束、`~graph_run()`|析构 op-state|
|`destroy_sender`|`~graph()`|析构 arena 里的 sender 副本|

`connect` 那一句（[dag.h:136](app://localhost/epitaxy/demo/dynamic-dag/dag.h:136)）值得看一眼：

```cpp
::new (operation_storage) operation_type(
    ::stdexec::connect(*static_cast<const Sender*>(sender), node_receiver{run, index}));
```

`connect` 返回的是 prvalue，靠**保证的复制消除**直接在 `operation_storage` 里构造——所以 op-state 不可移动也没关系，它从出生就在最终位置上。

---

## 六、完整走一遍（8 节点渲染图）

**构图期（一次）**

```
add_node("cull", then(schedule(sch), λ), {})
    → sender 副本进 sender_arena_
    → operation_offset = 0，arena 长到 96 字节
    → vtable = &node_vtable_for<decltype(sender)>::value
add_node("shadow", ..., {cull})
    → operation_offset = 96
    → edges_.push_back({cull, shadow})
...
finalize() → 建 CSR、收根、算深度
```

**执行期（每次）**

```
sync_wait(graph.run())
  graph_sender::connect(receiver) → graph_op<R> 落在 sync_wait 的栈上
  graph_op::start()                            [dag.h:775]
      挂上外层 stop token 的回调（两路取消合并成一路）
      launch()                                 [dag.h:599]
          for 全部 8 个节点: vtable->connect(...)   ← 8 个 op-state 一次性全建好
          counters[i] = predecessor_count[i]
          poisoned[i] = false
          outstanding = 8
          atomic_thread_fence(release)          ← 保证上面的初始化对 worker 可见
          schedule_start(cull)
              → vtable->start(op0) → 提交给线程池
```

然后 worker 线程上：

```
cull 的 lambda 跑完
  → then 的 op-state 调 node_receiver::set_value()   [dag.h:311]
  → graph_run::on_node_done(cull)                    [dag.h:722]
  → retire(cull, poison=false)                       [dag.h:678]
        successors 是 [shadow, gbuffer, ui]
        counters[shadow].fetch_sub(1) == 1  → schedule_start(shadow) → 提交
        counters[gbuffer].fetch_sub(1) == 1 → schedule_start(gbuffer) → 提交
        counters[ui].fetch_sub(1) == 1      → schedule_start(ui) → 提交
        outstanding.fetch_sub(1) == 8, 不是 1，返回
...
present 跑完
  → retire(present)
        没有后继
        outstanding.fetch_sub(1) == 1  → finish()      [dag.h:745]
              complete(error={}, stopped=false)
              → set_value(receiver_)  ← sync_wait 醒了
~graph_op → ~graph_run    [dag.h:579]
      析构 8 个 op-state
      把 run_storage 还给池        ← 下次执行不再分配
```

注意 `lighting` 有 3 个前驱，它的 `counters` 从 3 开始，前两个前驱 `fetch_sub` 返回 3 和 2（不启动），第三个返回 1（启动）。**扇入就这么一行。**

`acq_rel` 那个内存序（[dag.h:698](app://localhost/epitaxy/demo/dynamic-dag/dag.h:698)）同时干两件事：计数，以及建立 happens-before —— 读到 0 的那个线程一定看得见三个前驱写的所有数据。

---

## 七、三个坑对应的代码

这三处是"看起来多余、其实删了就炸"的地方：

**① op-state 不能在自己的 completion 里析构。** `set_value` 是从 op-state 内部调出来的，此刻它自己的栈帧还在。所以 `retire` 里**没有**析构，统一推到 [dag.h:579](app://localhost/epitaxy/demo/dynamic-dag/dag.h:579) 的 `~graph_run()`。arena 反正预留了，不额外花钱。

**② 内联完成会打穿栈。** 如果节点在 `start()` 里就完成了（inline scheduler、结果已就绪、或者被跳过），调用链是 `start_node → set_value → retire → schedule_start → start_node → ...`，**深度 = 图的深度**。 [dag.h:640](app://localhost/epitaxy/demo/dynamic-dag/dag.h:640) 的蹦床把它摊平：最外层那次调用负责排干 `pending` 队列，嵌套的启动请求只入队不递归。场景 4(b) 那条 20 万节点的链就是专门打这一点的——没有蹦床必爆。

**③ 失败也必须减计数。** [dag.h:728](app://localhost/epitaxy/demo/dynamic-dag/dag.h:728) 的 `on_node_error` 最后一句是 `retire(index, true)`，**不是** `return`。 直觉上"失败了就别管后继了"，但少减一次 `outstanding_` 就永远到不了 0 → 整图挂死。所以照常减计数，只是同时把后继标成 `poisoned`，后继启动时在 [dag.h:665](app://localhost/epitaxy/demo/dynamic-dag/dag.h:665) 看到毒标记就直接退休（skip）而不执行。

毒标记用 `relaxed` 存就够了，因为紧跟着的 `fetch_sub(acq_rel)` 会把它发布出去。

---

## 八、建议的阅读顺序

1. `retire()` [dag.h:678](app://localhost/epitaxy/demo/dynamic-dag/dag.h:678) —— 全部算法在这
2. `launch()` [dag.h:599](app://localhost/epitaxy/demo/dynamic-dag/dag.h:599) —— 一次执行怎么起来的
3. `add_node()` [dag.h:366](app://localhost/epitaxy/demo/dynamic-dag/dag.h:366) —— arena offset 怎么算出来的
4. `node_vtable_for` [dag.h:123](app://localhost/epitaxy/demo/dynamic-dag/dag.h:123) —— 类型擦除
5. `graph_op` [dag.h:760](app://localhost/epitaxy/demo/dynamic-dag/dag.h:760) —— 对外那层 sender 皮
6. 其余（`chunk_arena`、`finalize` 的 CSR、storage 池）都是纯机械代码，可以最后看

哪一节还是没通，指给我，我拿具体的调用序列或者内存图展开。

脱离你现在的代码，给我介绍下整体的思路

# 动态 DAG on execution —— 整体思路

## 一、起点：DAG 和 execution 有一个根本矛盾

execution 的所有权模型是**树**。

sender 组合出来的是一棵表达式树，`connect` 之后变成一棵嵌套的 op-state 树：父的 op-state 内联包含子的 op-state。这就是"结构化并发"的全部内容,也是它零开销的来源 —— 一次 `connect` 把整棵树摊成**一个对象**,生命周期由外向内层层包住,没有一个指针需要引用计数。

DAG 有**菱形**。菱形意味着一个节点有两个"父"。树形所有权表达不了这件事:谁拥有 `gbuffer` 的 op-state?`ssao` 和 `lighting` 都依赖它。

所以做动态 DAG,**必须在某处打破树形所有权**。这个代价不可避免。整个设计要回答的只有一句话:

> **在哪里打破,打破几次。**

---

## 二、标准库的答案,以及它为什么必须那样

`split` 就是标准库的答案:**在每一个共享点打破一次**,用引用计数把那个点变成共享所有权。菱形的合流端用 `when_all` 的倒计数收拢。

这是**通解**,而且标准库只能给通解 —— 因为它看到的信息是局部的:"这里有个 sender 被多个人用"。它不知道你有多少节点、边长什么样、图什么时候构完。它甚至不知道存在一个"图"。

代价是按共享点线性摊开的:每个共享点一次堆分配 + 一圈原子引用计数,再加结果 variant 和等待者链表。N 个节点大概 O(N) 次分配、O(E) 次引用计数操作。

**这不是标准库写得差,是它掌握的信息比你少。**

---

## 三、第一个关键观察:你有一个图边界,标准库没有

"运行期构图一次、然后反复执行"这个前提里藏着一个东西:**存在一个明确的时刻,图构完了**。那一刻你知道全部节点、全部边、全部拓扑。

有了这个边界,就可以做一件标准库做不到的事:**把所有权整体上提到边界这一层**。

- 一个对象拥有全部 N 个 op-state。
- 节点之间不再互相持有,只用**索引**指来指去。
- 索引不是所有权 —— 菱形于是不再是问题。

洞只打一个(图这一层),不是每个共享点打一个。

这一步不是"优化",是**利用了标准库没有的信息**。想清楚这点很重要,因为它决定了这套东西什么时候适用、什么时候不适用。

---

## 四、第二个关键观察:控制依赖下,广播和倒计数是同一个东西

把两个组合子拆开看它们的机械构成:

||本质|需要什么|
|---|---|---|
|`split`|跑一次 + 广播给 N 个等待者|引用计数 + 等待者链表 + **结果 variant**|
|`when_all`|等 N 个人到齐|倒计数器|

如果边**不携带值**(控制依赖),`split` 就不需要存结果、不需要 variant。剩下的"广播"退化成"通知 N 个后继"。而"通知一个后继"要做的事,就是**给它的倒计数器减一**。

于是:

- 扇出(一个节点 N 个后继)= 一个 `for` 循环
- 扇入(一个节点 N 个前驱)= `--counter == 0` 这个判断

两者合起来,**每个节点只需要一个整数**。

这不是巧合。DAG 调度的经典算法(Kahn 拓扑排序)就是这个计数器,教科书写法。sender 只是在外面包了一层壳。**所以真正的问题从来不是"怎么调度 DAG",而是"怎么把这个众所皆知的计数器塞进 sender 的所有权模型里"。**

---

## 五、由此得到的分层:把成本推到最早的那一层

零开销的定义就是:**执行期不做任何本可以在更早的层做完的事**。所以先把时间尺度分清:

|层|频率|这一层知道什么|应该在这层做完什么|
|---|---|---|---|
|编译期|—|每个节点 sender 的**具体类型**|op-state 的尺寸、对齐|
|构图期|运行期,**一次**|拓扑:节点数、边、根|内存布局、后继表、深度/优先级|
|执行期|运行期,**每帧**|只有"谁完成了"|只剩计数器加减 + 一次间接调用|

执行期允许剩下的东西:**每条边一次原子减,每个节点一次间接调用**。就这些。没有分配、没有引用计数、没有 `std::function`、没有虚函数(除了整图完成那一跳)。

---

## 六、支点:类型擦除必须放在哪一层

这一步最不显然,也是全部设计的枢轴。**如果只记一件事,记这个。**

op-state 的类型 = f(Sender, Receiver)。这两个参数的可知时刻不一样:

- **Sender**:每个节点都不同,但在**构图期是已知的完整类型**(节点是模板函数加进来的)。
- **Receiver**:是外层给的。`sync_wait(图)` 还是 `when_all(图A, 图B)`?构图期**不知道**。

现在推论:

> 如果让节点的 receiver 依赖外层 receiver,那 op-state 的尺寸就要等到执行期才知道 → 布局算不出来 → 只能退回每节点一次分配。

所以:**必须给所有节点一个固定的、与外层 receiver 无关的 receiver 类型。**

它只能持有一个**类型擦除的"图运行实例"指针**,把完成事件打到那里。外层 receiver 的类型只在最后一跳(整图完成)才出现。

一旦定下这一点,剩下的全是机械推导,没有任何自由度了:

1. 固定 receiver → op-state 尺寸构图期可知 → **一个 arena + 一组 offset**
2. Sender 类型要在构图期之后消失,但之后还要能 connect / start / 销毁 → **每个类型一份静态 vtable**(几个函数指针,在 `.rodata`,零分配)
3. 类型擦除的"图运行实例"需要把最终结果交给有类型的外层 receiver → **全程唯一一个虚调用**

这三条不是我选的,是从"arena 尺寸必须在构图期可知"这一个约束里挤出来的。

---

## 七、sender 模型强加的三条铁律

这三条也不是设计选择,是模型逼出来的。**任何人做同样的事都会撞上,而且都会先踩一遍再爬出来。**

### ① op-state 不能在自己的完成回调里被销毁

完成回调是从 op-state **内部**调出来的 —— 它自己的栈帧还在。销毁它就是在自己脚下拆地板。

→ 销毁必须推迟到整图结束后统一进行。 → 好消息:arena 反正已经预留了那些字节,推迟不花钱。这是上提所有权的额外红利。

### ② 完成可能是内联的,于是递归深度 = 图深度

`start()` 里就完成的情况很常见:inline scheduler、结果已就绪、节点被跳过。此时调用链变成 `启动 → 完成 → 启动下一个 → 完成 → ...`,**深度等于图的深度**。

图有 20 万层就是 20 万层栈帧。

→ 必须有**蹦床**:嵌套的启动请求只入队,由最外层那次调用排干队列,把递归摊平成循环。 → 这条最阴险,因为小图上永远不出问题。

### ③ 失败和取消必须走和成功完全相同的计数路径

直觉是"这个节点失败了,后继不用跑了,直接返回"。这个直觉会造成挂死。

因为**终止条件是"所有节点都退休了"**。少减一次计数,条件就永远不满足。

→ 失败时照常减计数,只是**另外**打一个标记告诉后继"你可以退休了,但别真执行"。 → 推论:终止检测必须独立于成功/失败。所以除了每条边的计数器,还需要一个全局的"还剩几个节点没退休"。

这三条的共同点:**它们都是"看起来多余、删掉之后小图照跑、大图或异常路径必炸"的代码。** 所以我在 demo 里专门给每条配了一个会打脸的场景。

---

## 八、放弃了什么(这部分比上面重要)

一个设计不说代价就是营销。

**图必须先构完再执行。** 执行途中发现新节点(你说的 case C)不支持 —— 那需要在执行中扩容计数器数组和 arena,而扩容就要面对"正在被别人读的数组能不能搬家"。可以做,但那是另一套设计,不是这套加个函数。

**边不携带值。** 数据必须走旁路 context。类型安全由你自己负责,编译器不帮你查"lighting 读的那个字段 gbuffer 真的写了吗"。这是最实质的让步 —— 换来的是边的存储是 0 字节。想要数据依赖,得在 arena 里再开一层带类型的槽位,复杂度会明显上一个台阶。

**节点粒度是你自己切的。** 图层不会帮你合并小节点。反过来说,节点切太碎的话,调度开销会盖过工作量 —— 实测每个节点大约 0.4~1 µs 的调度成本,所以节点体最好在几十微秒以上。**这件事对最终性能的影响,很可能比 arena 省下的那几百字节大一个数量级。** 真正成熟的引擎会在构图期做"链熔合"(把无分支的链压成一个节点),那是编译器 IR pass 的思路,而不是调度器的思路。

**部分失败的语义是固定的:下游跳过,不相关的分支照跑。** 这通常是你想要的,但如果你想要"某个分支失败了,整帧丢弃"或者"降级到备用节点",那是策略,得另外加。

---

## 九、一个和图完全无关、但能把并行度打到 1 的坑

这套模型的提交模式是:**节点完成后,在完成它的那个线程上提交后继。**

很多线程池对"从池内线程提交"做了本地队列优化 —— 任务先进本线程的私有队列,攒够一批才发布给别人窃取。这个优化对"一个任务生成一堆子任务"的场景是对的,对 DAG 是灾难:**扇出通常只有 2~3 个后继,永远攒不够一批**,于是后继全滞留在完成者的私有队列里被它自己顺序跑完,其余线程一直睡着。

我在 demo 里实测到的就是这个:4 个各 3.6 ms 的任务,池外提交用 4 个线程 3.9 ms,池内提交用 **1 个线程 14.8 ms**。

**教训不是"这个池不好",而是:任何"在完成线程上扇出后继"的调度器都要去确认它底下那个池的窃取策略。** 这跟你的图层实现一行代码都无关,但它会让所有努力归零。

---

## 十、什么时候**不**该这么做

如果图只有几十个节点,或者执行频率不高(不是每帧),那 `split` + `when_all` 拼出来的版本几十行就能写完,而且不用面对上面那三条铁律。**那才是正确选择。**

这套东西的前提是:**每帧都跑、节点上千、且你已经确认调度开销出现在了 profile 里。** 否则你付出的是几百行需要维护的、涉及内存序和生命周期的代码,换回来的是 profile 上看不见的收益。

---

## 十一、放回你的真实场景

你的上下文是 Vulkan 渲染。有一件事值得先说清楚:

**CPU 侧的 DAG 和 GPU 侧的 DAG 是两张图。** 上面这一整套调度的是 CPU 上的命令录制、剔除、更新;而"shadow pass 的结果被 lighting pass 读取"是 GPU 侧的依赖,那是 barrier 和 timeline semaphore 的事,跟 CPU 上谁先谁后没有必然关系。

所以 senders 在渲染里的真正杠杆,恐怕不是这个 CPU 调度器,而是**一个 `vk_scheduler`** —— 让 op-state 的完成挂在 timeline semaphore 上,于是 GPU 的完成也能变成一个 sender,和 CPU 的工作在同一套模型里组合。那才是 execution 这套抽象在图形里最值钱的地方。这个 CPU DAG 调度器是它的地基,不是它本身。

---

## 一句话总结

> DAG 破坏了 execution 赖以实现零开销的树形所有权。标准库只能在每个共享点用引用计数各打一个洞,因为它不知道图在哪里结束。而"构图一次、反复执行"这个前提给了你一个边界,于是所有权可以整体上提到图这一层,菱形退化成索引。再加上"边不带值",扇出和扇入合并成每节点一个整数。剩下唯一的技术难点是:**让所有节点共用一个固定的 receiver 类型**,好让 op-state 的尺寸在构图期就算出来 —— 这一步之后,其余的一切都是被迫的推导,包括那三条会先咬你一口的生命周期铁律。

能否通过模板配置的方式，像exec::any_sender/receiver/scheduler那样，来最大化抽象 例如我在配置图的时候我知道我需要一个什么样的动态图（需不需要传递数据），这些都是编译期信息，我可以提前确认 以及还有静态反射的手段 我想知道如果引入以上两个思路可以做成什么样子，哪些参数可以被模板化，静态反射可以做什么 我的目的是追求最大灵活度，并且0开销，与C++标准的设计是一致的

# 规格驱动的 DAG 内核 + 静态反射

能做,而且这正是这套东西该走的方向。但先把"模板化到底买了什么"说清楚,否则很容易做成一个 256 个实例化、编译半分钟、还是要付一次间接调用的怪物。

---

## 零、先分清"0 开销"的两层含义

Stroustrup 那句话其实是两条:

1. **不用的东西不花钱**(don't pay for what you don't use)
2. **用了的东西不比手写更贵**(what you do use is as efficient as hand-coding)

我现在的内核解决的是**第 2 条**。模板化能解决的是**第 1 条** —— 而且只有第 1 条。

这个区分很重要,因为它划定了模板化的天花板:

> **真正异构、真正运行期发现的图,必然要付一次间接调用。模板化消除不了它。模板化的价值是:让"其实不需要那么动态"的场景不为动态性付钱。**

所以正确的目标不是"用模板让动态图变成零开销",而是**"让规格里没要求的能力,在生成的代码里彻底不存在"**。这跟 `std::span<T, N>` 让 size 成员消失、`never_stop_token` 让停止回调状态消失,是同一件事。P2300 自己就在用这个手法,你的方向和标准是一致的。

---

## 一、先修正我前面一个说窄了的结论

上一轮我说:

> 必须给所有节点一个**固定的、与外层 receiver 无关的** receiver 类型。

后半句是对的,**前半句我说过头了,而这个差别恰好决定了数据依赖能不能做**。

真实约束只有一条:**节点 receiver 的类型不能依赖外层 Receiver**(否则 arena 尺寸在构图期算不出来)。

它**完全可以依赖节点自己的 Sender** —— 因为 Sender 在 `add_node` 里就是完整类型。所以:

```
node_receiver<T>        // T = 该节点的结果类型,来自它自己的 completion signatures
```

是合法的,arena 布局照样算得出来。这一条松开之后,数据依赖就通了。下面第三节展开。

---

## 二、第一层:把内核变成"规格驱动"

`exec::any_sender_of<Completions, Queries>` 的精髓是:**模板参数是一份接口规格,不是一个实现**。你声明"我需要哪些完成签名、哪些 env query 要转发",它据此生成刚好够用的 vtable。

DAG 内核可以照抄这个形状。核心性质是:**规格对整张图是统一的,所以它不破坏 arena 那个支点。**

```cpp
template<class Spec> class basic_graph;

// 用法上像这样(名字随便,形状是重点)
using render_graph = dag::basic_graph<dag::spec<
    dag::data_dependency,                       // 边带值
    dag::completions<set_value_t(), set_error_t(std::exception_ptr), set_stopped_t()>,
    dag::node_queries<get_stop_token_t, get_scheduler_t>,
    dag::concurrent,
    dag::on_failure::skip_downstream,
    dag::dispatch::indirect
>>;

using init_graph = dag::basic_graph<dag::spec<
    dag::control_dependency,
    dag::completions<set_value_t()>,            // 不会失败、不会取消
    dag::single_threaded,
    dag::static_capacity<64>
>>;
```

### 策略 → 消掉什么的对照表

这张表是"哪些参数可以被模板化"的直接答案。左边是策略,右边是在生成的代码里**彻底消失**的东西(不是被 `if` 跳过,是不存在):

|策略|消掉的状态|消掉的代码路径|
|---|---|---|
|`no_cancellation`|`inplace_stop_source`、`stopped_` 标志、`graph_op` 里的 `optional<stop_callback>`、node_env 的 token 成员|`set_stopped` 全路径、外层 token 挂钩/摘钩、每个节点启动前的 `stop_requested()` 检查|
|**节点 completion 无 `set_error`**|`error_`、`error_claimed_`、`error_ready_`、`poisoned[]`|`set_error` 全路径、首错 CAS、毒传播|
|**上面两条同时成立**|—|**蹦床可以整个去掉** —— 因为"跳过"不再存在,唯一的内联完成来源变成 inline scheduler(见下一条)|
|`assume_asynchronous_nodes`|—|蹦床的 thread_local 与排干循环|
|`single_threaded`|所有 `atomic<>` 退化成裸整数|全部 fence、全部 `acq_rel`;`retire()` 变成纯标量运算|
|`dispatch::homogeneous`(全图同一个 sender 类型)|`nodes_[i].vtable`、`nodes_[i].operation_offset`|**vtable 完全消失**,`start` 是直接调用、可内联;offset 退化成 `i * sizeof(Op)`|
|`dispatch::closed_set<S...>`(节点类型是闭集)|每类型 vtable|间接调用 → `switch`,编译器可去虚化并内联热分支|
|`on_failure::continue_anyway`|`poisoned[]`|毒传播|
|`static_capacity<N>`|三个 `unique_ptr`、storage 池、池的 mutex|`acquire/release_storage` 全部|
|`no_tracing`|`tracer_`、`tracer_user_`|每个事件点的空指针检查|
|`control_dependency`|结果 arena、每节点 1 bit 的"结果已构造"|结果的构造/析构/读取|
|`counter_width<uint8_t>`(入度上界已知)|每节点 3 字节|— (纯 cache footprint)|

### 两个端点

**最小端**:`single_threaded + no_cancellation + 不失败 + homogeneous + static_capacity<N> + control_dependency`

塌缩成:

```
array<Op, N> ops;  array<uint8_t, N> counters;  一个循环
```

没有原子、没有间接调用、没有分配、没有虚函数、没有蹦床。**就是一个静态 Kahn 调度器**,和你手写的一模一样。

**最大端**:全动态、并发、异构、可失败、可取消 —— 就是我现在写的那份。

**关键在于:同一份源码。** 这正是你说的"与标准的设计一致"。

---

## 三、第二层:数据依赖怎么做成静态类型安全且零开销

这是你问的"需不需要传递数据"那一半,也是我 demo 里唯一真正让步的地方(旁路 `frame_context`,类型安全靠自己)。它是可以补上的。

### 3.1 句柄带上类型

```
node_index          →   node_handle<T>      // 就是个 uint32_t + 幻影类型,0 字节开销
```

`add_node` 返回 `node_handle<T>`,`T` 从该节点 sender 的 `completion_signatures` 里推出来。前驱以 `node_handle<Ts>...` 传入,于是**"这个节点读的类型和上游产出的类型不匹配"是编译错误**,不是运行期 UB。

### 3.2 结果槽:第二个 arena,同一个 offset 手法

每个节点在结果 arena 里有一个 `T_i` 的槽位,offset 在 `add_node` 时就算出来(和 op-state 完全一样的累加)。`node_receiver<T>::set_value(T&& v)` 就地构造进槽位,然后走原来的 `retire`。

**没有新的间接调用** —— `T` 在 `node_vtable_for<Sender>::connect` 里是已知的完整类型,`set_value` 是模板成员,直接内联成一次 placement new。

### 3.3 读取端:一个 `inputs` sender

这里有个真实的时序难题:**`connect` 发生在输入存在之前**。所以不能让用户写 `add_node(f(inputs))` —— 构图时输入还不存在。

解法是给图加一个 sender 工厂:

```cpp
auto lighting = g.add_node("lighting",
    g.inputs(shadow, gbuffer, ssao)      // sender,完成签名 = set_value_t(const S&, const G&, const A&)
        | continues_on(sch)
        | then([](const S& s, const G& gb, const A& a) -> Lit { ... }));
```

`g.inputs(...)` 返回的 sender:connect 时只记下 `graph_run*` 和几个 offset,start 时读槽位、内联 `set_value`。

这个形状的好处是**节点侧完全不受限** —— 用户照旧可以接 `continues_on` / `upon_error` / `let_value` / 任意自定义算法。图只管"喂进去"和"接出来"两端,中间是开放的。

> **这是我认为整个设计里最该保住的性质:内核侧封闭(编译期规格),节点侧开放(任意 sender)。** 一旦为了做数据依赖去规定节点的 sender 形状,灵活度就崩了。

### 3.4 唯一的新成本,以及一个新的正确性约束

- **成本**:每节点 1 bit"结果已构造",供拆解时决定要不要析构(被跳过的节点没有结果)。
- **约束**:`data_dependency` 和 `on_failure::continue_anyway` **互斥** —— 下游会读到未初始化的槽。这必须 `static_assert` 掉。

这也预示了下面那个风险:策略之间不独立。

---

## 四、第三层:静态反射(P2996 / 注解 P3394)能做什么

先给结论,因为这个最容易被高估:

> **反射的价值在"作图/声明"这一层和"编译期验证"这一层,基本不在执行内核。** 内核已经接近手写,反射没什么可省的。

但在作图层,它能做的事相当多,而且有几件是**别的手段做不了**的。

### 4.1 从函数签名**推导数据依赖**(最实用的一条)

```cpp
struct gbuffer_pass {
    gbuffer operator()(const visibility_set&, const view_config&) const;
};
```

`parameters_of` 拿到参数类型列表 → 在已注册节点里找 `visibility_set` 的唯一生产者 → **边自动连上**。找不到或有歧义 → 编译错误。

于是 `add_node(name, sender, {preds...})` 变成 `add_node<gbuffer_pass>()`,拓扑从**类型系统里长出来**,而不是手工维护一张边表。手工边表是这类系统里 bug 最集中的地方(漏一条边 = 数据竞争,多一条边 = 白丢并行度),而它恰恰是编译器能替你查的。

**局限要说清楚**:同一个 pass 类型有 N 个实例时(4 级 shadow cascade),按类型查生产者就歧义了,必须显式给句柄或加注解消歧。

由此得到一个我觉得很干净的分工:

> **反射管形状,运行期管基数。** 静态骨架从签名和注解里推出来;实例个数(几级 cascade、几个 view、几个 tile)留给运行期。这刚好对上前面讨论过的"四种动态性来源"里的前三种。

### 4.2 声明式作图 + 注解携带策略

```cpp
struct render_graph {
    [[= dag::root]]                        cull_pass     cull;
    [[= dag::scheduler(gpu_queue)]]        shadow_pass   shadow;
    [[= dag::priority(critical)]]          gbuffer_pass  gbuffer;
    [[= dag::enabled_if(&config::ssao)]]   ssao_pass     ssao;
};
```

`members_of` + 读注解 → 拓扑、调度器、优先级、条件启用全都是编译期信息。图的结构变成**可被审查的声明**,而不是散落在几十次 `add_node` 调用里的过程性代码。

`enabled_if` 那条特别值:**条件启用**(前面归类的第 3 种动态性)可以在编译期把每个 feature 组合的计数器初值表都算出来,运行期只是选表,`counters[]` 的初始化连加法都不用做。

### 4.3 用 `define_aggregate` **生成**旁路 context

这条直接补掉了我 demo 里承认的那个弱点。

现在的 `frame_context` 是手写的,字段和节点对不上是没人管的。有了反射:

```
节点列表  →  define_aggregate  →  struct frame_results {
                                      visibility_set cull;
                                      gbuffer        gbuffer;
                                      ...
                                  };
```

字段**由节点集生成**,访问是 `results.gbuffer`(有名字、有类型、编译器查),而不是 offset 加 `reinterpret_cast`。**类型安全从"靠你自己"变成"编译期保证",而且运行期布局和手算 offset 完全一样。**

### 4.4 生成闭集派发,替掉函数指针

反射能枚举出全部节点类型,据此生成一个闭集 dispatcher(`switch` 或生成的 `variant`)。相比现在的函数指针 vtable:编译器能看见全部目标,可以内联热节点、可以做更好的分支预测,并且**没有间接调用**。

这是反射能触及执行内核的**唯一一处**,前提是节点类型集在编译期封闭。

### 4.5 编译期验证 —— 把 assert 变成 static_assert

拓扑一旦是 constexpr 数据,下面这些全部可以在编译期证明:

- 无环
- 每个被消费的类型都有生产者、且唯一
- 没有节点读取"可能被跳过的上游"的结果而不处理该情况
- 关键路径长度在预算内
- 所有节点的 completion 都在图声明的 completions 之内
- 策略组合自洽(比如上面那个 `data_dependency` × `continue_anyway`)

我现在这些是 `assert` —— 也就是"跑到了才知道"。这一条的价值可能不亚于性能。

### 4.6 反射**不能**做什么(别抱幻想)

- **不能**把运行期发现的拓扑变成编译期常量。节点数是运行期值,offset 就必须是运行期值。
- **不能**消除真异构 + 真动态情况下的那次间接调用。
- 注解只活在编译期。运行期构出来的图仍然要走完整的动态路径 —— 所以**两条路径都得留着**,反射只是让能走静态路的场景走静态路。
- 求 completion signatures 的并集、算错误类型的 variant 这类事,今天用模板元编程已经能做(stdexec 内部就在做)。反射让它**好写**,但不是**使能者** —— 别把它算成收益。

---

## 五、放在一起的形状

```
                        ┌─────────────────────────────────────┐
   声明层(可选)         │ struct render_graph { [[=dag::…]] … }│  ← 反射读注解与签名
                        └──────────────────┬──────────────────┘
                                           │ 编译期:拓扑、依赖、策略、验证
                        ┌──────────────────▼──────────────────┐
   作图层               │ basic_graph<Spec>::add_node<T>()      │  ← 静态路与动态路
                        │ basic_graph<Spec>::add_node(sender,…) │     共用同一份内核
                        └──────────────────┬──────────────────┘
                                           │ 构图期:arena 布局、CSR、深度
                        ┌──────────────────▼──────────────────┐
   内核层               │ 计数器 + 后继表 + arena              │  ← Spec 决定这里
                        │ (原子性/取消/错误/派发 由 Spec 裁剪) │     还剩下什么
                        └──────────────────┬──────────────────┘
                        ┌──────────────────▼──────────────────┐
   对外                 │ 一个合规 sender                      │
                        └─────────────────────────────────────┘
```

节点侧始终是**任意 sender**,这是灵活度的来源。规格只裁剪内核。

---

## 六、风险,以及我建议怎么防

### 组合爆炸

8 个二元策略 = 256 个实例化。代码膨胀、编译时间、调试符号爆炸。

**防法**:内核实现里**不要按策略特化**,而是按"能力"用 `if constexpr` 分支 + 用空基类/`[[no_unique_address]]` 让状态消失。特化只用于真正改变数据结构的那两三个(`homogeneous`、`static_capacity`、`single_threaded`)。

### 策略不独立

上面已经撞到一个(`data_dependency` × `continue_anyway`)。还有:`homogeneous` 和"任意 sender"是矛盾的;`no_cancellation` 和"节点内部用了 stop token"是矛盾的。

**防法**:规格本身要有一个 `static_assert` 层,而不是把矛盾留给用户在运行期发现。这层本身就是反射的好客户。

### 规格的表达力上限

`Spec` 一旦超过 5~6 个轴,就没人能读懂 `basic_graph<...>` 的声明了。

**防法**:提供命名的预设(`dag::frame_graph`、`dag::init_graph`、`dag::job_graph`),让 `Spec` 成为专家逃生口而非日常接口 —— 就像 `std::pmr` 之于 allocator。

### 反射的时间线

P2996 进了 C++26,注解(P3394)也在。但编译器实现成熟度参差,大规模用在生产代码里还早。

**防法**:**现在就按反射的形状设计接口**(节点是带 `operator()` 的类型、依赖体现在签名里、策略以类型形式表达),中间用手写 traits 或宏顶着。等编译器到位,替换的是推导层,不是内核。这样反射不是重写,是把手工填的东西自动填上。

---

## 七、标准里的先例(说明这条路是"正统"的)

|标准设施|用的手法|
|---|---|
|`std::span<T, N>` vs `std::span<T>`|extent 做模板参数 → size 成员消失|
|`never_stop_token` / `unstoppable_token`|停止能力做类型 → 停止回调状态与检查全部消失|
|`exec::any_sender_of<Completions, Queries>`|**模板参数是接口规格,vtable 按规格生成**|
|`unique_ptr<T, Deleter>`|无状态 deleter 占 0 字节|
|`allocator_traits` / `pmr`|策略聚合成 traits,并提供命名预设|

你要的东西在标准里没有对应设施,但**手法全都有先例**。第 3 行就是这个 DAG 内核该直接照抄的那个。

---

## 八、如果只挑三件事做

按"收益 / 成本"排:

1. **`node_handle<T>` + 结果 arena + `graph.inputs(...)`** —— 补上数据依赖,纯模板即可,不等反射。这是当前设计最大的功能缺口。
2. **`Spec` 里先做 4 个轴**:`single_threaded`、`no_cancellation`、`无 set_error`、`static_capacity<N>`。这四个消掉的状态和代码最多,且互相基本独立,不会引发组合爆炸。特别是前三个同时成立时**蹦床整个消失**,那是很大一块复杂度。
3. **反射先只做一件事:从签名推导依赖 + 编译期验证无环/生产者唯一。** 不碰派发、不碰内核。这条的收益是**正确性**,而正确性上的收益在这类系统里通常比性能值钱 —— 漏一条边就是数据竞争,而它恰好是编译器能查的。

`define_aggregate` 生成 context 和闭集派发,可以放在后面 —— 前者是锦上添花,后者只在节点类型闭集时有效,而闭集的场景往往本来就可以直接 `homogeneous`。

---

要不要我挑其中一条,写一份能编译的原型出来?我建议从第 1 条(数据依赖的静态类型化)开始 —— 它不依赖反射,能立刻验证"`node_receiver<T>` 依赖自己的 sender 不破坏 arena 支点"这个关键假设是否真的成立。