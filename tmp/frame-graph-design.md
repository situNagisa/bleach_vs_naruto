# 帧任务图架构设计

> 基于 stdexec 的每帧单发射任务图。目标：main 只负责收集 entity、构图、执行；entity 开发者只负责描述依赖和实现任务逻辑。

---

## 1. 需求与约束

### 参与者

**render（vulkan）** —— 一个 entity，负责：
1. `render begin`
2. `render end`
3. 等待 fence（**在每一帧末等待，而非每一帧开始**）

**entity** —— 一个概念而非具体基类，每个由不同开发者实现。默认是纯计算任务。当它的任务涉及与 render 同步时，它负责：
1. 计算
2. `render begin` 后 record command
3. 等待 fence 后清理资源

**main** —— 知晓所有 entity 的具体类型，负责：
1. 收集所有 entity
2. 构建任务图
3. 执行任务图
4. 循环 1~3 直到退出

### 硬约束

| # | 约束 | 本设计如何满足 |
|---|---|---|
| 1 | 用 stdexec | 图 = sender 表达式；只额外写了 `node_ref` 和 `when_all_range` |
| 2 | 每帧重新构建任务图，单发射，帧间隔离 | `Frame` 对象每帧构造/析构，不可拷贝不可移动；entity 上没有帧字段 |
| 3 | 不涉及 render 的 entity 不引入任何 render 概念 | 纯计算 entity 不 include `render/*`，不出现 `node_ref`、不出现类型擦除 |
| 4 | entity 与 render 的同步由它们自己完成，main 不特殊处理 | main 对所有 entity 只调用 `begin_frame()` + `frame_node()`；同步全部写在 entity 自己的节点里 |

---

## 2. 核心决策

### 2.1 图就是 sender 表达式

stdexec 已经提供了完整的图语义，不要另造一套：

| 图的概念 | stdexec 原语 |
|---|---|
| 边 / 顺序依赖 | `then`、`let_value` |
| 汇聚（等多个前驱） | `when_all` |
| 扇出（一个节点多个后继，只执行一次） | `split` |
| 拓扑排序 | 表达式树的嵌套结构本身 |
| 调度器归属 | `starts_on`、`continues_on` |
| 取消传播 | stop token 经 `when_all` 自动传播 |
| 错误汇总 | `let_error` / `sync_wait` 返回值 |
| 驱动一帧 | `sync_wait` |

**没有** GraphBuilder、没有 Tag 表、没有运行期拓扑排序、没有节点闭包计算。

### 2.2 节点粒度 = entity 内部的任务节点

一个 entity 在图里通常不是一个节点。`Terrain` 有三个：`compute` / `record` / `cleanup`，它们在图里的位置完全不同（record 夹在 begin 和 end 之间，cleanup 在 fence 之后）。

### 2.3 依赖 = 持有对方的具体类型，调用它的节点访问器

程序是静态的，entity 依赖谁在开发时可知，所以不需要名字解析：

```cpp
f.template job<Renderer>().begin_node(f)   // 依赖 render.begin
```

递归调用天然产生拓扑序，`node_ref` 的记忆化保证只执行一次。

### 2.4 Entity / Job / Frame 三层生命周期

| 层 | 生命周期 | 持有什么 |
|---|---|---|
| `Entity` | 整个程序 | 长生命周期资源（device、mesh、fence/command pool 的 N 槽环形缓冲） |
| `Entity::Job` | 一帧 | 本帧图的状态（`node_ref` 记忆槽）+ 本帧数据（command buffer、slot 索引、staging handle） |
| `Frame` | 一帧 | 所有 Job 的 tuple；是 Job 之间互相查找的媒介 |

**Entity 上没有任何"当前帧"字段。** 帧间隔离因此是结构性的，不靠纪律：
- `Frame` 删除了拷贝和移动构造 → 图无法逃出一次循环迭代
- `node_ref` 没有 `reset()` → 不存在"忘了复位、沿用上一帧 split 状态"这个 bug 类别
- 上一帧的中间结果物理上不存在

> 例外说明：Vulkan 的 per-frame 资源（fence / command pool / semaphore 的 N 槽环）**必须**跨帧存活，归 Entity 所有。约束 2 约束的是**图和图的中间状态**，不是资源池；Job 只持有"我用第 i 槽"这个索引。

### 2.5 render.end 的名单在启动期读取

`render.end` 必须在所有录制者之后，但 render 不知道谁在录制。采用 **push 模型**：录制者在构建期把自己的 record 节点注册进 `render::roster`。

push 模型有构建顺序依赖：`Frame::run` 遍历 job tuple 调 `frame_node`，谁先谁后不确定；只要有一个录制者排在 Renderer 后面就会被漏掉。

**关键观察**：stdexec 的惰性模型免费给了一条分界线 —— 整张图在 `connect` 阶段就已完全构建完毕，`start` 之后才开始执行。

```
        connect(expr)                    start(op)
  ───────────────┼──────────────────────────┼──────────────────────►
   构建期：所有 frame_node 被调用            启动期：begin 完成后
           所有 register 发生                let_value 的函数体才运行
```

把名单读取放进 `let_value`，构建顺序就彻底无关了。顺带还解决了 pull 模型（静态遍历 job tuple 筛选）表达不了的两件事：
- **本帧条件性录制**：被剔除就根本不注册，连 `compute_node` 都不会被构建
- **插件**：动态加载的录制者不在 main 的 entity 列表里，无法被静态遍历到

---

## 3. 一帧的图形态

```
   physics.step ──────────────────────────────────────┐  (纯计算 entity，不知道 render 存在)
                                                      │
   terrain.compute ─┐                                 │
                    ├──> terrain.record ─┐            │
   render.begin ────┤                    │            │
        (split)     ├──> ui.record ──────┤            │
                    └──> ...             │            │
                                         │            │
                       roster (启动期读) ─┴─> render.end
                                                      │
                                                      v
                                              render.fence  (帧末等待, split)
                                                      │
                                    ┌─────────────────┴─────────────────┐
                                    v                                   v
                             terrain.cleanup                       ui.cleanup
                                    │                                   │
                                    └──────────> 顶层 when_all <────────┘
                                                      ^
                                                      └─ physics.step
```

---

## 4. 框架代码

框架总量约 150 行，分三个头文件。

### 4.1 `core/node.hpp` —— 具名共享节点

```cpp
using node_sender = exec::any_sender_of<
    stdexec::set_value_t(),
    stdexec::set_error_t(std::exception_ptr),
    stdexec::set_stopped_t()>;

// 一个"具名共享节点"的记忆槽。无 reset —— 生命周期就是 Job 的生命周期。
class node_ref {
public:
    node_ref() = default;
    node_ref(const node_ref&) = delete;

    template <class Fn> node_sender get(Fn&& fn) {
        if (!impl_) {
            if (building_) throw std::logic_error{"frame graph dependency cycle"};
            building_ = true;
            impl_ = wrap(stdexec::split(fn()));        // 扇出交给 stdexec
            building_ = false;
        }
        return impl_->subscribe();
    }
private:
    struct iface { virtual ~iface() = default; virtual node_sender subscribe() const = 0; };
    template <class S> struct impl final : iface {
        S s; explicit impl(S x) : s(std::move(x)) {}
        node_sender subscribe() const override { return node_sender{s}; }  // split 后可拷贝
    };
    template <class S> static std::shared_ptr<iface> wrap(S s) {
        return std::make_shared<impl<S>>(std::move(s));
    }
    std::shared_ptr<iface> impl_;
    bool building_ = false;
};
```

**关于类型擦除的成本**：只有扇出节点需要 `node_ref`，而扇出节点无论如何都要过 `split` 的共享 op-state —— 那里已经是间接调用，`any_sender_of` 再套一层几乎不增加成本。单消费者节点不用 `node_ref`：`auto` 返回、全静态、零分配。**只有真正扇出的地方付钱**（典型一帧里是 `render.begin`、`render.fence` 和少数 compute 节点）。

### 4.2 `core/frame.hpp` —— 每帧的 Job 容器

```cpp
template <class... Es> struct World {                   // 长生命周期
    stdexec::scheduler auto sched;
    std::tuple<Es&...> entities;
    FrameInfo info;                                      // dt、帧号、输入快照
};

template <class... Es>
class Frame {                                            // 每帧一个，帧末析构
public:
    explicit Frame(World<Es...>& w)
        : w_(w), jobs_(std::get<Es&>(w.entities).begin_frame(w)...) {}

    Frame(const Frame&) = delete;
    Frame(Frame&&)      = delete;                        // 结构性地防止逃出本帧

    World<Es...>& world() { return w_; }

    template <class E> auto& job() {
        static_assert((std::is_same_v<E, Es> || ...),
                      "依赖了一个 main 没有收集的 entity");
        return std::get<index_of_v<E, Es...>>(jobs_);
    }

    void run() {                                         // 构图 + 执行，sender 不逃出本函数
        stdexec::sync_wait(when_all_tuple(
            tuple_map(jobs_, [this](auto& j) { return j.frame_node(*this); }))).value();
    }
private:
    World<Es...>& w_;
    std::tuple<job_t<Es, World<Es...>>...> jobs_;
};
```

Job 的节点访问器对 `Frame` 类型取模板参数（`auto& f`），避免 `Terrain.hpp` ↔ `Frame.hpp` 的头文件循环。

`job<E>()` 的 `static_assert` 把"main 忘了收集某个 entity"变成编译错误。

### 4.3 `core/when_all_range.hpp` —— 动态 `when_all`

`render.end` 要汇聚运行期数量的录制节点。`when_all` 是变参的，用不了。

**不用 `async_scope::spawn` + `on_empty()`**，因为它四重错误：
1. 错误不传播（`spawn` 要求 `set_value()` 完成，错误 terminate，得手动收集重抛）
2. stop 不传播（上层取消传不到录制节点；录制节点失败也不取消兄弟）
3. 每个 spawn 一次分配
4. 语义不对 —— `on_empty()` 是"scope 空了"，不是 join，不携带子操作的完成信号

因为名单是**同质且无值**的（`std::vector<node_sender>`，完成签名固定），自己写一个只需约 90 行：不需要 `transform_completion_signatures`，不需要 value tuple 的 variant 折叠。前面接受的类型擦除在这里回本。

```cpp
namespace fg {

// 让不可移动的 op-state 在目标地址就地构造（stdexec 内部同款手法）
template <class F> struct conv {
    F f;
    using type = std::invoke_result_t<F>;
    operator type() && { return std::move(f)(); }   // C++17 保证省略，不走移动构造
};
template <class F> conv(F) -> conv<F>;

template <class S, class R> struct range_op;

template <class S, class R> struct child_rcvr {
    using receiver_concept = stdexec::receiver_t;
    range_op<S, R>* op;

    void set_value() noexcept                     { op->arrive(); }
    void set_error(std::exception_ptr e) noexcept { op->fail(std::move(e)); op->arrive(); }
    void set_stopped() noexcept                   { op->stopped();          op->arrive(); }
    auto get_env() const noexcept {               // 子节点看到的是本 op 的 stop token
        return stdexec::prop{stdexec::get_stop_token, op->stop_.get_token()};
    }
};

template <class S, class R>
struct range_op {
    using child      = child_rcvr<S, R>;
    using child_op_t = stdexec::connect_result_t<S, child>;

    range_op(std::vector<S> cs, R r)
        : rcvr_(std::move(r)), src_(std::move(cs)), n_(src_.size()),
          ops_(std::make_unique<std::optional<child_op_t>[]>(n_)) {}

    range_op(range_op&&) = delete;

    void start() & noexcept {
        if (n_ == 0) { stdexec::set_value(std::move(rcvr_)); return; }

        pending_.store(n_ + 1, std::memory_order_relaxed);   // +1 记在启动循环自己名下，
                                                             // 保证循环期间 op 不会被析构
        on_stop_.emplace(stdexec::get_stop_token(stdexec::get_env(rcvr_)), fwd_stop{this});

        for (std::size_t i = 0; i < n_; ++i)                 // 先全部 connect
            ops_[i].emplace(conv{[&] {
                return stdexec::connect(std::move(src_[i]), child{this});
            }});
        src_.clear();

        for (std::size_t i = 0; i < n_; ++i)                 // 再全部 start
            stdexec::start(*ops_[i]);

        arrive();                                            // 交还那个 +1
    }

    void fail(std::exception_ptr e) noexcept {
        int expect = 0;
        if (state_.compare_exchange_strong(expect, 1, std::memory_order_acq_rel)) {
            err_ = std::move(e);
            stop_.request_stop();                            // fail-fast：取消其余录制者
        }
    }
    void stopped() noexcept {
        int expect = 0;
        if (state_.compare_exchange_strong(expect, 2, std::memory_order_acq_rel))
            stop_.request_stop();
    }
    void arrive() noexcept {
        if (pending_.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
        on_stop_.reset();
        switch (state_.load(std::memory_order_acquire)) {
        case 0:  stdexec::set_value(std::move(rcvr_));                   break;
        case 1:  stdexec::set_error(std::move(rcvr_), std::move(err_));  break;
        default: stdexec::set_stopped(std::move(rcvr_));                 break;
        }
    }

    struct fwd_stop { range_op* op; void operator()() noexcept { op->stop_.request_stop(); } };
    using stop_cb = stdexec::stop_token_of_t<stdexec::env_of_t<R>>
                        ::template callback_type<fwd_stop>;

    R rcvr_;
    std::vector<S> src_;
    std::size_t n_;
    std::unique_ptr<std::optional<child_op_t>[]> ops_;   // 定长数组 => 可容纳不可移动类型
    std::atomic<std::size_t> pending_{0};
    std::atomic<int> state_{0};                          // 0=value 1=error 2=stopped，首个胜出
    std::exception_ptr err_;
    stdexec::inplace_stop_source stop_;
    std::optional<stop_cb> on_stop_;
};

template <class S>
struct range_sender {
    using sender_concept = stdexec::sender_t;
    using completion_signatures = stdexec::completion_signatures<   // 同质无值 => 可以写死
        stdexec::set_value_t(),
        stdexec::set_error_t(std::exception_ptr),
        stdexec::set_stopped_t()>;

    std::vector<S> children;
    template <stdexec::receiver R> auto connect(R r) && {
        return range_op<S, R>{std::move(children), std::move(r)};
    }
};

template <class S> auto when_all_range(std::vector<S> v) {
    return range_sender<S>{std::move(v)};
}
}
```

**三个必须注意的实现细节**（上面已处理）：

- **`connect` 全部做完再 `start`**。否则第一个同步完成的子节点可能在后面的子节点还没 connect 时就把整个 op 完成掉，进而析构尚未 connect 的存储。
- **`pending_` 预置为 `n + 1`**。启动循环自己占一份，循环体内不可能触发完成和析构；循环末尾 `arrive()` 才交还。少了这一手，全部子节点同步完成时会在循环中间析构 `this`。
- **`unique_ptr<optional<T>[]>` + `conv`**。`connect_result_t` 不可移动，所以既不能用 `vector`（增长要求可移动），也不能用 `optional::emplace(连接结果)`（那是移动构造）。定长数组 + 转换运算符就地构造是唯一干净的组合。

> 如果手上的 stdexec 版本已自带 `when_all_vector` / `when_all_range` 之类的范围版本，直接用它，本文件可删。

### 4.4 `render/recorder.hpp` —— 录制者名单

```cpp
namespace render {
class roster {                                  // 本帧对象，Renderer::Job 持有
public:
    void add(node_sender s) {
        if (sealed_) throw std::logic_error{"录制者注册晚于 render.end 启动"};
        v_.push_back(std::move(s));
    }
    std::vector<node_sender> seal() { sealed_ = true; return std::move(v_); }
private:
    std::vector<node_sender> v_;
    bool sealed_ = false;
};
}
```

`sealed_` 是关键：把"漏掉一个录制者"从**静默丢帧内容**变成**一次明确的异常**。没有这个标志，这套推迟机制的失败模式是不可见的。

---

## 5. 使用方

### 5.1 Renderer

```cpp
class Renderer {                                   // 长生命周期：device + N 槽资源环
public:
    static constexpr std::uint32_t kSlots = 3;

    class Job {
    public:
        Job(Renderer& r, World_& w)
            : r_(r), w_(w), slot_(w.info.index % kSlots) {}

        // ── 共享节点：多个 entity 依赖 => node_ref ──
        node_sender begin_node(auto& f) {
            return begin_.get([&] {
                return stdexec::starts_on(f.world().sched, stdexec::just())
                     | stdexec::then([this] { cmd_ = r_.begin_frame(slot_); });
            });
        }

        node_sender fence_node(auto& f) {
            if (phase_.load() == phase::recording)
                throw std::logic_error{"录制节点依赖了 fence/end —— 环"};
            return fence_.get([&] {
                return end_node(f)
                     | stdexec::continues_on(r_.fence_sched())   // 阻塞等待，独占线程
                     // error / stopped 两条路径都必须落到 wait_fence，见 §7
                     | stdexec::let_error([this](std::exception_ptr e) {
                           r_.wait_fence(slot_);
                           return stdexec::just_error(std::move(e)); })
                     | stdexec::let_stopped([this] {
                           r_.wait_fence(slot_);
                           return stdexec::just_stopped(); })
                     | stdexec::then([this] { r_.wait_fence(slot_); });
            });
        }

        node_sender frame_node(auto& f) { return fence_node(f); }   // 本 Job 的 sink

        render::roster& recorders() { return roster_; }
        VkCommandBuffer cmd() const { return cmd_; }                // 给录制者用

    private:
        enum class phase { constructing, recording, submitting };

        node_sender end_node(auto& f) {
            return end_.get([&] {
                return begin_node(f)
                     | stdexec::let_value([this] {
                           // ── 启动期 ── 全图已 connect 完毕，名单已齐
                           phase_.store(phase::recording);
                           return fg::when_all_range(roster_.seal());
                       })
                     | stdexec::continues_on(w_.sched)
                     | stdexec::then([this] {
                           phase_.store(phase::submitting);
                           r_.submit(slot_);
                       });
            });
        }

        Renderer& r_;  World_& w_;  std::uint32_t slot_;
        VkCommandBuffer cmd_{};                     // 本帧数据，帧末随 Job 消失
        render::roster roster_;
        std::atomic<phase> phase_{phase::constructing};
        node_ref begin_, end_, fence_;
    };

    Job begin_frame(World_& w) { return Job{*this, w}; }

private:
    exec::static_thread_pool fence_sched_{1};       // fence 阻塞等待专用
};
```

`Renderer::Job` 顺便成了"本帧渲染上下文"：`cmd_`、`slot_` 这些最容易被写成 `Renderer` 成员、然后在多线程录制里踩到的东西，现在天然是每帧独立对象的成员。

### 5.2 参与渲染的 entity

```cpp
class Terrain {
public:
    explicit Terrain(Renderer& r) : r_(r) {}        // 主动持有依赖的具体类型

    class Job {
    public:
        Job(Terrain& t, World_& w) : t_(t), w_(w) {}

        auto frame_node(auto& f) {
            auto& rj = f.template job<Renderer>();
            if (t_.visible())                              // 本帧被剔除 => 根本不注册
                rj.recorders().add(record_node(f, rj));    // 构建期 push，顺序无关
            return rj.fence_node(f)                        // ← 等 fence 后清理，自己声明
                 | stdexec::then([this] { t_.release(staging_); });
        }

    private:
        node_sender record_node(auto& f, auto& rj) {       // 单消费者 => 不需要 split
            return stdexec::when_all(compute_node(f), rj.begin_node(f))
                 | stdexec::continues_on(f.world().sched)  // 必须，见 §7 晚订阅
                 | stdexec::then([this, &rj] { staging_ = t_.record(rj.cmd()); });
        }
        node_sender compute_node(auto& f) {
            return compute_.get([&] {
                return stdexec::starts_on(f.world().sched, stdexec::just())
                     | stdexec::then([this] { mesh_ = t_.compute(w_.info.dt); });
            });
        }
        Terrain& t_;  World_& w_;
        Mesh mesh_{};  StagingHandle staging_{};           // 本帧数据
        node_ref compute_;
    };

    Job begin_frame(World_& w) { return Job{*this, w}; }
private:
    Renderer& r_;
};
```

三条职责一一对应：`compute_node` 计算、`record_node` 在 begin 后录制、`frame_node` 在 fence 后清理。

### 5.3 纯计算 entity

```cpp
class Physics {
public:
    class Job {
    public:
        Job(Physics& p, World_& w) : p_(p), w_(w) {}
        auto frame_node(auto& f) {
            return stdexec::starts_on(f.world().sched, stdexec::just())
                 | stdexec::then([this] { p_.step(w_.info.dt); });
        }
    private:
        Physics& p_;  World_& w_;
    };
    Job begin_frame(World_& w) { return Job{*this, w}; }
};
```

没有 `node_ref`、没有类型擦除、没有 render，全静态零分配。约束 3 由"没写"满足。

### 5.4 main

```cpp
int main() {
    exec::static_thread_pool pool{std::thread::hardware_concurrency()};

    Renderer render{device};
    Physics  physics;
    Terrain  terrain{render};
    Audio    audio;

    World world{pool.get_scheduler(), std::tie(render, physics, terrain, audio), {}};

    while (!world.info.quit) {
        Frame frame{world};      // 1+2. 收集本帧 Job（构造即收集）
        frame.run();             // 3.   构图 + 执行
        world.info.advance();
    }                            //      frame 析构：图、split 状态、本帧数据全部消失
}
```

循环体三行，没有任何状态复位调用 —— 复位就是 `frame` 出作用域。没有 begin/end/fence，也没有任何两个 entity 之间的连线。

---

## 6. entity 开发者需要知道的全部约定

1. 提供 `Job begin_frame(World&)`，`Job` 持有本帧状态。
2. `Job` 提供 `frame_node(auto& f)`，返回本 entity 本帧**所有 sink 的汇聚**（有孤立节点就 `when_all(sink, orphan)`，否则它不会被执行）。
3. 依赖别人 = `f.template job<Peer>().xxx_node(f)`。
4. 有扇出的节点用 `node_ref` 包一层；只有一个消费者的节点直接 `auto` 返回。
5. 每个节点自己 `continues_on(f.world().sched)`，不要假设自己在哪条线程上被启动。
6. 要参与渲染：include `render/recorder.hpp`，把 record 节点 `add` 进 `f.job<Renderer>().recorders()`；要在 fence 后清理就依赖 `fence_node`。

---

## 7. 已知陷阱与对策

| 陷阱 | 原因 | 对策 |
|---|---|---|
| **订阅已完成的 `split` 会就地同步执行** | `let_value` 里连接录制节点时，`begin_node` 已经完成；`split` 对已完成节点立即投递缓存结果，投递发生在 begin 的完成线程上 | 每个录制节点内部的 `continues_on(sched)` **不是可选的**，去掉会把全部录制串行到一条线程 |
| **环从构建期错误退化成启动期死锁** | 名单读取推迟后，录制节点若误依赖 `fence/end`，构建期一切正常；启动期去订阅一个正在执行中的 `split`（我们此刻就在它的 `let_value` 里），挂进等待队列永不返回 | `Renderer::Job::phase_`，在 `recording` 窗口内调 `fence_node`/`end_node` 直接抛。覆盖整个录制窗口，连录制节点内部再套 `let_value` 的延迟订阅也能抓到 |
| **录制异常导致 fence 被跳过** | stop 现在真的会传播：一个录制节点失败 → `when_all_range` 取消其余 → `end_node` 以 error 完成 → fence 收到 error 而非 value → cleanup 也被跳过 → 资源泄漏 + 下一帧复用正被 GPU 读的 slot | fence 节点 `let_error` + `let_stopped` + `then` 三条路径都落到 `wait_fence(slot_)`。**这是整套设计里最需要写测试的一处** |
| **注册晚于启动** | 插件或某个 Job 在 `start` 之后才注册 | `roster::sealed_` 抛异常，第一帧必现 |
| **main 忘收集某 entity** | — | `Frame::job<E>()` 的 `static_assert` 编译期报错 |
| **编译期成本** | 单消费者路径全静态，类型会传播 | `node_ref` 天然是类型擦除边界；编译墙上凿洞时给某个节点主动包一层 `node_sender` 即可切断传播 |

---

## 8. 演进方向

**帧内插件。** `roster` 是具体类型，插件拿不到 `Frame<Es...>`。给 `Renderer::Job` 加一个抽象接口：

```cpp
namespace render {
struct frame_ctx {
    virtual roster&         recorders() = 0;
    virtual node_sender     begin()     = 0;
    virtual node_sender     fence()     = 0;
    virtual VkCommandBuffer cmd() const = 0;
};
}
```

插件只 include `render/recorder.hpp`，编译期完全不需要知道 main 那份 entity 列表。

**CPU / GPU 流水化。** 目前 `Frame` 不可移动，堵住了这条路。真要做重叠时改成 `std::unique_ptr<Frame>` 交给 `exec::async_scope`：

```cpp
scope.spawn(stdexec::just(std::make_unique<Frame>(world))
          | stdexec::let_value([](auto& f) { return f->sender(); }));
```

帧对象活到该帧的异步操作结束，仍然没有跨帧共享状态，图的结构一行不改。

**启动期的其它用途。** `end_node` 那个 `let_value` 是"全图已构建完毕"的唯一时刻，任何需要全局信息的决策都可以放进去：按实际录制者数量决定用几个 secondary command buffer、按 pipeline 排序后再提交、根据注册进来的 pass 数选 render pass 变体。

**帧 arena。** 一帧目前的分配：每个扇出节点一次 `split` + 一次 `node_ref::wrap`，`roster` 的 vector，`when_all_range` 的 op-state 数组。既然 `Frame` 的生命周期就是一帧，全部挂到帧 arena 上是顺手的事。

---

## 9. 待验证清单

写成可编译示例时应覆盖：

- [ ] 注册顺序打乱：Renderer 排在 job tuple 第一个 vs 最后一个，录制结果一致
- [ ] 录制者本帧被剔除：`compute_node` 不被构建，`render.end` 正常提交
- [ ] 空名单：`when_all_range({})` 立即以 `set_value` 完成，begin → end 直连
- [ ] 单个录制者抛异常：其余录制者被取消，fence 仍执行，异常传到 main
- [ ] 上层取消整帧（`sync_wait` 外部 request_stop）：fence 仍执行
- [ ] 故意制造环（录制节点依赖 `fence_node`）：触发 `phase_` 检测而非死锁
- [ ] 帧间隔离：连续两帧，第二帧的 `node_ref` 全部为空、Job 全部新建
