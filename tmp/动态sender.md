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

||CPU 线程池节点|GPU / IO 节点|
|---|---|---|
|完成时机|`start()` 返回前后不久，同一个 worker 上|`start()` 立刻返回，几十 ms 后由 poll 线程/reaper 回调|
|op-state 生命周期|短，可紧凑复用|必须跨越挂起点存活|
|后继启动位置|可以内联在当前 worker 上继续（cache 热）|必然跨线程，且可能需要 hop 回去|

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