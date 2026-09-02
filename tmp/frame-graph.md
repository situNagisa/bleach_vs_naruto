现要处理这么个任务，任务描述如下 render（vulkan）： 是一个entity，负责：

1. 开启渲染render begin
2. render end
3. 等待fence（注意fence是在每一帧末等待而非每一帧开始）

entity： 是一个概念，并非具体的类 每个entity都有不同开发者开发 entity有一个任务，不特殊说明则默认为纯计算任务 当一个entity的任务涉及与render任务的同步，它负责：

1. 计算
2. render begin后record command
3. 等待fence后清理资源

main函数： 知晓所有entity的具体类型 负责：

1. 收集所有entity
2. 构建他们的任务图
3. 执行任务图
4. 循环1~3，直到退出

帮我设计一个架构，能够优雅的处理上述场景，使对于main来说，他只负责构建图并执行；对于entity开发者来说，它只负责描述图（依赖哪些其它entity），并实现具体的任务逻辑

约束：

1. 用stdexec
2. 所有的entity是每帧构建出任务图，任务图单发射，下一帧要重新构建任务图
3. 如果一个entity不涉及render，那么不应该为它引入任何render相关的概念
4. entity与render之间的同步是他们自己进行的，不能在main中特殊处理render与entity的同步关系

跟现有仓库的具体代码没有太大关系，我在为现有仓库设计新架构

# 架构设计：Tag-based 单发射帧任务图

## 核心思路

约束 3、4 决定了一件事：**main 不能有"render 阶段"的概念**。所以 render 必须是一个普通 entity，它与其它 entity 的同步必须走一条 _通用的、entity 之间的_ 依赖机制。

关键设计点是：

1. 图的节点粒度**不是 entity，而是 entity 内部的"任务节点"**。一个 entity 可以 declare 多个节点（compute / record / cleanup），它们在图里的位置完全不同。
2. 依赖不用"entity 名"表达，而用 **Tag（命名同步点）**。一个节点 `provides` 若干 tag，`after` 若干 tag。依赖一个 tag = 依赖**所有** provide 它的节点。
3. 这个"一对多 tag"顺带解决了**反向依赖**问题：`render.end` 必须在所有 record 之后，但 render 不知道谁在录制。做法是 render.end `after` tag `render.record`，而每个需要录制的 entity 把自己的 record 节点 `provides` 这个 tag。**barrier 由被依赖方声明，参与者自己加入。**

于是 main 只有一个通用规则：拓扑排序 tag 图 → 物化 sender → 执行。render 对它而言和物理、音频没有区别。

---

## 一帧的图形态

```
        physics.step ─────────────────────────┐   (纯计算 entity，完全不知道 render)
                                              │
  terrain.compute ─┐                          │
                   ├─> terrain.record ─┐      │
  render.begin ────┤                   │      │
    [provides      ├─> ui.record ──────┤      │
     render.begin] │                   │      │
                   └─> ...             │      │
                                       │      │
              [tag render.record] ─────┴──> render.end
                                              │  [provides render.end]
                                              v
                                        render.fence   (帧末等待)
                                              │  [provides render.fence]
                              ┌───────────────┴───────────────┐
                              v                               v
                       terrain.cleanup                   ui.cleanup
```

`render.begin / render.record / render.end / render.fence` 这四个 tag 定义在 `render/sync.hpp`。**只有需要与 render 同步的 entity 才 include 它**；main 不 include（main 只 include `render/renderer.hpp` 去构造对象）。

---

## 核心层（`core/`，零 render 概念）

```cpp
// 类型擦除的 sender，用于同质容器
using AnySender = exec::any_sender_of<
    stdexec::set_value_t(),
    stdexec::set_error_t(std::exception_ptr),
    stdexec::set_stopped_t()>;

// 编译期字符串 hash，避免运行期字符串比较
struct Tag {
    std::uint64_t id;
    constexpr explicit Tag(std::string_view s) : id(fnv1a(s)) {}
};

// 一个已物化节点的"可多次订阅"句柄：内部持有 split 后的 sender
class Port {
public:
    template <stdexec::sender S>
    explicit Port(S s)
        : impl_(std::make_shared<Impl<decltype(stdexec::split(std::move(s)))>>(
              stdexec::split(std::move(s)))) {}

    AnySender subscribe() const { return impl_->subscribe(); }

private:
    struct IFace {
        virtual ~IFace() = default;
        virtual AnySender subscribe() const = 0;
    };
    template <class Split> struct Impl final : IFace {
        Split s;                                    // split 后是 copyable / multi-shot
        explicit Impl(Split x) : s(std::move(x)) {}
        AnySender subscribe() const override { return AnySender{s}; }
    };
    std::shared_ptr<const IFace> impl_;
};
```

`stdexec::split` 是这里的关键原语：它把单发射 sender 变成可被多个消费者连接的共享 sender，且只执行一次。这正好匹配"单发射 DAG 的 fan-out"。

```cpp
struct NodeDecl {
    std::string_view            name;      // 隐式 provides Tag{name}
    std::vector<Tag>            provides;
    std::vector<Tag>            after;
    // 延迟物化：declare 阶段依赖还不存在，所以只交出工厂
    std::function<AnySender(AnySender deps)> build;
};

class GraphBuilder {
public:
    explicit GraphBuilder(stdexec::scheduler auto sched);
    void add(NodeDecl d);          // entity 唯一需要调用的 API
    FrameGraph build() &&;         // 拓扑排序 + 环检测 + 物化
};

class FrameGraph {
public:
    void run();                    // 执行到全部完成；抛出本帧第一个错误
};
```

Entity 的"概念"就是一行：

```cpp
template <class E>
concept Entity = requires(E& e, GraphBuilder& gb, const FrameContext& f) {
    e.declare(gb, f);
};
```

---

## `build()` 做什么

1. 建 `tag_id -> vector<node_index>` 表（每个节点的 `name` 也算一个 provider）。
2. 边：对节点 N 的每个 `after` tag，向所有 provider 连边。
3. 拓扑排序；有环则报错并打印 tag 链（这是 entity 开发者最常见的错误，诊断信息要好）。
4. 按拓扑序物化：
    
    ```cpp
    AnySender deps = fold_when_all(  // 空依赖 => stdexec::just()    for each after-tag, for each provider p : ports[p].subscribe());ports[n] = Port{ decl[n].build(std::move(deps)) };
    ```
    
    零 provider 的 tag 折叠为 `just()` —— 于是"没有任何 entity 录制"这一帧自动退化成 begin → end，无需特判。
5. 执行：把每个节点的 `subscribe()`（先 `upon_error` 把异常存进帧 error slot，避免 `async_scope` terminate）`spawn` 进 `exec::async_scope`，然后 `stdexec::sync_wait(scope.on_empty())`。因为是 `split`，重复连接同一节点不会重复执行，所以可以无脑连接全部节点，不必区分 sink。

> 用 `async_scope` 而不是把 N 个节点塞进一个 `when_all`，是为了避免节点数带来的类型爆炸和递归折叠深度。

---

## main（全部内容）

```cpp
int main() {
    exec::static_thread_pool pool{std::thread::hardware_concurrency()};

    Renderer render{device};
    Physics  physics{};
    Terrain  terrain{render};   // 需要与 render 同步 => 构造期注入服务
    Audio    audio{};           // 纯计算，构造签名里没有 render
    auto entities = std::tie(render, physics, terrain, audio);

    FrameContext frame{};
    while (!quit) {
        GraphBuilder gb{pool.get_scheduler()};
        std::apply([&](auto&... e) { (e.declare(gb, frame), ...); }, entities);
        std::move(gb).build().run();
        frame.advance();
    }
}
```

没有一行同步逻辑，也没有 begin/end/fence 的字样。

---

## render 侧

```cpp
// render/sync.hpp —— 只有"要和 render 同步的 entity"才 include
namespace render::sync {
inline constexpr Tag begin    {"render.begin"};
inline constexpr Tag record   {"render.record"};   // barrier：录制者自己加入
inline constexpr Tag submitted{"render.end"};
inline constexpr Tag fence    {"render.fence"};
}
```

```cpp
void Renderer::declare(GraphBuilder& gb, const FrameContext&) {
    gb.add({ .name = "render.begin", .provides = {sync::begin},
             .build = [this](AnySender d) {
                 return std::move(d) | stdexec::then([this] { begin_frame(); }); }});

    gb.add({ .name = "render.end", .provides = {sync::submitted},
             .after = {sync::begin, sync::record},          // ← 反向依赖
             .build = [this](AnySender d) {
                 return std::move(d) | stdexec::then([this] { end_frame(); }); }});

    gb.add({ .name = "render.fence", .provides = {sync::fence},
             .after = {sync::submitted},
             .build = [this](AnySender d) {
                 return std::move(d)
                      | stdexec::continues_on(fence_sched_)   // 专用单线程，别占 pool
                      | stdexec::then([this] { wait_fence(); }); }});
}
```

`fence_sched_` 是 renderer 自己持有的 `static_thread_pool{1}`。`vkWaitForFences` 是阻塞调用，绝不能占用计算池的线程 —— 这个决定属于 render 的实现细节，main 不该知道。

## 一个参与渲染的 entity

```cpp
void Terrain::declare(GraphBuilder& gb, const FrameContext& f) {
    gb.add({ .name = "terrain.compute",
             .build = [this, &f](AnySender d) {
                 return std::move(d) | stdexec::then([this, &f] { compute(f.dt); }); }});

    gb.add({ .name = "terrain.record", .provides = {render::sync::record},
             .after = {Tag{"terrain.compute"}, render::sync::begin},
             .build = [this](AnySender d) {
                 return std::move(d) | stdexec::then([this] { record(r_.frame()); }); }});

    gb.add({ .name = "terrain.cleanup",
             .after = {render::sync::fence},
             .build = [this](AnySender d) {
                 return std::move(d) | stdexec::then([this] { release_staging(); }); }});
}
```

## 一个纯计算 entity

```cpp
void Physics::declare(GraphBuilder& gb, const FrameContext& f) {
    gb.add({ .name = "physics.step", .provides = {Tag{"physics.transforms"}},
             .build = [this, &f](AnySender d) {
                 return std::move(d) | stdexec::then([this, &f] { step(f.dt); }); }});
}
```

它的头文件、构造函数、declare 全都不出现 render —— 满足约束 3。

---

## 几个值得注意的细节

**为什么图的边不携带数据。** 让边只表达顺序，跨 entity 的数据（command buffer、变换数组）通过构造期注入的服务引用访问。否则通用图就得处理任意 value 类型，类型擦除会失控。需要更松的耦合时，在 `FrameContext` 上加一个按类型索引的 service registry，`f.get<Renderer>()` —— 但 include `render/sync.hpp` 的 entity 反正已经知道 render 了，构造注入更直白也更安全。

**错误与取消。** 每个节点物化时统一接一层 `upon_error`：记录到 `FrameGraph` 的 error slot，并 request_stop 整帧的 `inplace_stop_source`。这样一个 entity 抛异常不会把 `async_scope` 打成 terminate，且 `render.fence` 节点必须无条件跑（用 `let_stopped` 让它在取消路径上也执行），否则 GPU 资源泄漏。这是"cleanup 语义"的必要处理。

**每帧重建的成本。** `NodeDecl` 里的 `std::function` 和 `vector<Tag>` 每帧都在分配。用一个每帧重置的 monotonic arena 作为 `GraphBuilder` 的分配器，把 `std::function` 换成指向 arena 的 `function_ref` + 显式生命周期（arena 活到 `run()` 返回之后）。约束 2 要求每帧重建，所以这里是唯一的热点，值得从一开始就设计对。

**跨帧流水线。** 如果以后想让 fence 等的是**上一帧**的 fence（真正的 CPU/GPU 并行），机制已经够了：把上一帧 `render.fence` 的 `Port` 存进 `FrameContext`，本帧 `render.fence` 节点由 renderer 自己从 `FrameContext` 取出并 `after` 它。仍然不需要 main 参与。当前需求是帧末等待，先不做。

---

要我把 `core/` 这层（`Port` / `GraphBuilder` / `FrameGraph` + 一个跑得起来的 fake renderer 示例）实际写出来吗？

你这个是动态图的逻辑，实际上整个程序是静态的，entity如果涉及render是可以引用render的具体类型的，entity可以主动的去获取依赖的entity的具体类型，这个是开发时可知的

# 静态帧图：编译期 DAG + stdexec

改成静态后，架构的骨架不变（**节点粒度 = entity 内部的任务节点**，而不是 entity；render 只是普通 entity），但三样东西全部下移到编译期：

|动态版|静态版|
|---|---|
|`Tag`（运行期字符串/hash）|节点**类型**本身|
|tag → providers 运行期查表|`using deps = deps_list<...>`，直接写依赖的具体类型|
|barrier group（谁 provide 我就等谁）|对 entity 列表做**编译期 trait 扫描**|
|运行期拓扑排序 + `std::function` + `any_sender_of`|递归 alias template 推出整帧的 sender 类型|

"每帧重建"依然满足：**类型固定，对象每帧新建**。帧图对象就是一个 tuple，构造成本是几次指针拷贝，`split` 的共享状态每帧全新，天然单发射。

---

## 1. 节点声明

节点是 entity 的嵌套类型，携带依赖和执行体：

```cpp
template <class Owner> struct node { using owner = Owner; };

struct Terrain {
    explicit Terrain(Renderer& r) : r_(r) {}       // 主动引用依赖的具体类型

    struct Compute : node<Terrain> {
        using deps = deps_list<>;
        static void run(Terrain& self, const Frame& f) { self.compute(f.dt); }
    };

    struct Record : node<Terrain> {
        using deps = deps_list<Compute, Renderer::Begin>;
        static void run(Terrain& self, const Frame&) { self.record(self.r_.cmd()); }
    };

    struct Cleanup : node<Terrain> {
        using deps = deps_list<Renderer::Fence>;
        static void run(Terrain& self, const Frame&) { self.release_staging(); }
    };

    using nodes        = node_list<Compute, Record, Cleanup>;
    using render_record = Record;      // ← 主动加入 render 的 record barrier
private:
    Renderer& r_;
};
```

纯计算 entity 就是：

```cpp
struct Physics {
    struct Step : node<Physics> {
        using deps = deps_list<>;
        static void run(Physics& self, const Frame& f) { self.step(f.dt); }
    };
    using nodes = node_list<Step>;
};
```

头文件、构造函数、`deps`、`run` 里都不出现 render — 约束 3 由"没写"直接满足，不需要任何隔离机制。

**默认 sender 形状**由框架给（`continues_on(default_sched) | then(run)`）。需要完全控制的节点改为提供 `make`：

```cpp
struct Renderer::Fence : node<Renderer> {
    using deps = deps_list<End>;
    static auto make(Renderer& self, const Frame&, auto&& deps_sender) {
        return std::move(deps_sender)
             | stdexec::continues_on(self.fence_sched())   // 阻塞等待，不占计算池
             | stdexec::then([&self] { self.wait_fence(); });
    }
};
```

`make` 是逃生舱：fence 换调度器、异步 IO 用 `let_value`、想 `upon_stopped` 保证清理必执行，都在这里，不污染框架。

---

## 2. barrier：`render.end` 怎么静态地"等所有录制者"

这是唯一真正需要设计的地方。render 不知道谁在录制，但 **main 知道全部 entity 类型**，而 main 本来就要把这个列表交给帧图。于是把"扫描列表"这件事变成一个**编译期查询**，由 render 自己发起：

```cpp
namespace render_detail {
template <class E> concept Records = requires { typename E::render_record; };

template <class E>
using record_of = std::conditional_t<Records<E>, deps_list<typename E::render_record>,
                                                 deps_list<>>;
template <class... Es>
using all_records = concat_t<record_of<Es>...>;
}

struct Renderer {
    struct Begin : node<Renderer> {
        using deps = deps_list<>;
        static void run(Renderer& self, const Frame&) { self.begin_frame(); }
    };

    struct End : node<Renderer> {
        template <class... Entities>                        // ← 需要世界视图的节点
        using deps_for = concat_t<deps_list<Begin>,
                                  render_detail::all_records<Entities...>>;
        static void run(Renderer& self, const Frame&) { self.end_frame(); }
    };

    struct Fence : node<Renderer> { /* 见上 */ };

    using nodes = node_list<Begin, End, Fence>;
};
```

框架的依赖解析：节点有 `deps` 就用它，有 `deps_for` 就喂进 entity 列表。

```cpp
template <class N, class Entities> struct deps_of;
template <class N, class... Es>
struct deps_of<N, type_list<Es...>> {
    using type = std::conditional_t<requires { typename N::deps; },
                                    typename_or_void<N>,                 // N::deps
                                    typename N::template deps_for<Es...>>;
};
```

这样 main 只做一件通用的事：把 entity 列表交给帧图。它不知道 `render_record` 是什么，也不知道 `Begin/End/Fence` 的先后 —— 约束 4 成立。任何 entity 都可以用同样的手法声明自己的 barrier（比如 `Physics::Solve` 等所有 `E::physics_contributor`）。

---

## 3. 帧图物化

依赖是类型，所以整帧的 sender 类型可以递归推出来。三个互相递归的 alias：

```cpp
template <class N, class W> using deps_sender_t =
    when_all_of_t<port_t<Ds, W>...>;                 // Ds = deps_of<N, entities_of<W>>
                                                     // 空依赖 => stdexec::just()
template <class N, class W> using raw_sender_t =
    decltype(invoke_make<N>(std::declval<W&>(), std::declval<deps_sender_t<N, W>>()));

template <class N, class W> using port_t =
    std::conditional_t<(consumer_count_v<N, W> > 1),
                       decltype(stdexec::split(std::declval<raw_sender_t<N, W>>())),
                       raw_sender_t<N, W>>;
```

`split` 依然是 fan-out 的关键原语（把单发射 sender 变成可多消费者连接、只执行一次的共享 sender）。但静态版能做动态版做不到的优化：**只有出度 > 1 的节点才 split**，出度为 1 的节点把 raw sender 直接 move 给唯一消费者，零共享状态、零分配。典型帧里只有 `Begin` / `Fence` / 少数 compute 节点需要 split。

运行期存储 = 节点闭包（所有 entity 的 `nodes` 沿 `deps` 求传递闭包）的 tuple，按需记忆化构建：

```cpp
template <class World>
class FrameGraph {
public:
    explicit FrameGraph(World& w) : w_(w) {}          // 全部 optional 为空

    void run() {
        std::apply([&](auto... sinks) {               // Sinks = 闭包中无人依赖的节点
            stdexec::sync_wait(stdexec::when_all(port<decltype(sinks)>()...)).value();
        }, sink_list_v);
    }

private:
    template <class N> auto& port() {
        auto& slot = std::get<std::optional<port_t<N, World>>>(ports_);
        if (!slot) slot.emplace(build<N>());          // 递归先建依赖 => 天然拓扑序
        return *slot;
    }
    template <class N> auto build() {
        return maybe_split<N>(invoke_make<N>(w_, deps_sender<N>()));
    }

    World& w_;
    tuple_of_optionals<closure_t<World>, World> ports_;
};
```

递归构建自带拓扑序，不需要排序算法。环 = 模板无限递归，所以前面加一道 constexpr 环检测把它变成可读的 `static_assert`。

`World` 持有 `std::tuple<Renderer&, Physics&, Terrain&, ...>` + scheduler + `Frame`；`N::owner` 直接 `std::get<Owner&>` 定位实例。

---

## 4. main

```cpp
int main() {
    exec::static_thread_pool pool{std::thread::hardware_concurrency()};

    Renderer render{device};
    Physics  physics;
    Terrain  terrain{render};        // entity 自己引用具体类型
    Audio    audio;

    World world{pool.get_scheduler(), std::tie(render, physics, terrain, audio)};

    while (!world.frame.quit) {
        FrameGraph{world}.run();     // 类型固定，对象每帧新建
        world.frame.advance();
    }
}
```

四行：收集、构图、执行、循环。没有 begin/end/fence，没有同步逻辑。

---

## 5. 静态化真正换来的东西

这不只是省掉运行期开销，而是把**图的正确性变成编译错误**。几条值得直接写进框架的断言：

```cpp
// 录制必须发生在 begin 之后（否则 command buffer 还没开）
static_assert((contains_v<transitive_deps_t<typename E::render_record, Entities>,
                          Renderer::Begin> && ...));

// 任何 cleanup 节点必须在 fence 之后（否则资源在 GPU 用完前就释放了）
static_assert(contains_v<transitive_deps_t<Terrain::Cleanup, Entities>, Renderer::Fence>);
```

第二条尤其重要：Vulkan 里"清理早于 fence"是那种只在特定驱动、特定帧率下随机崩的 bug。静态图能在编译期把它拦住。同理可以在编译期导出 graphviz（对节点闭包做 constexpr 遍历），把每帧的实际依赖关系画出来给 entity 开发者看。

## 6. 代价（说清楚）

- **编译期成本**：整帧是一个巨型 sender 类型，entity 多起来后是主要成本。缓解手段是把类型擦除变成**每个节点的 opt-in**：某个节点写 `using erase = std::true_type;`，框架就在它的 raw sender 外套一层 `exec::any_sender_of`，切断类型传播。默认全静态，只在编译墙上凿洞。
- **`deps_list` 里写具体类型 = 编译期耦合**。这是你要的（开发时可知），但意味着改一个节点名会波及依赖它的 entity。建议每个 entity 在自己头文件里给节点起稳定的公开别名（`Terrain::Record` 是接口，内部实现改名不影响）。
- 出度统计 `consumer_count_v` 需要先算全图闭包再回头决定是否 split，所以帧图的类型计算是两趟的（闭包 → 出度 → port 类型）。实现上要注意别让 `port_t` 直接依赖 `consumer_count_v` 的计算又反过来依赖 `port_t`。用"先算纯拓扑（只看 `deps`，不看 sender 类型）"这一趟解决：出度只需要 `deps`，和 sender 类型无关，所以可以独立先算完。

---

要我把 `core/` 这层实际写出来吗？`deps_list` / `closure_t` / `FrameGraph` / 那几条 `static_assert`，加一个 fake renderer + 两个 entity 的可编译示例。

stdexec已经有了一套构建图的逻辑，以及处理sender之间的依赖关系的逻辑，请你复用它

# 复用 stdexec：图就是 sender 表达式本身

你说得对 —— 我前两版把 stdexec 已经有的东西又实现了一遍。stdexec 的 sender 表达式**本身就是 DAG**，`connect/start` **本身就是拓扑执行**。对照一下：

|图的概念|stdexec 原语|我之前多余地造了什么|
|---|---|---|
|边 / 顺序依赖|`then`、`let_value`|—|
|汇聚（等多个前驱）|`when_all`|`deps_sender_t` 折叠|
|扇出（一个节点多个后继，只执行一次）|**`split`**|`Port` / `port_t`|
|拓扑排序|表达式树的嵌套结构|`GraphBuilder` 拓扑排序、`closure_t`|
|调度器归属|`starts_on`、`continues_on`|—|
|取消传播（一个节点失败，兄弟节点停下）|stop token 经 `when_all` 自动传播|—|
|错误汇总|`let_error` / `sync_wait` 返回值|帧 error slot|
|驱动一帧|`sync_wait` / `async_scope`|—|

所以 `GraphBuilder`、`Tag` 表、拓扑排序、闭包计算、`NodeDecl` **全部删掉**。

stdexec 唯一没给的，是**"给节点起个名字，让多个消费者找到同一个实例"**。因为 `split` 的结果得有人存着 —— 存哪、什么时候失效，是应用层的事。这就是全部要写的框架代码。

---

## 全部框架代码

```cpp
// core/node.hpp
using node_sender = exec::any_sender_of<
    stdexec::set_value_t(),
    stdexec::set_error_t(std::exception_ptr),
    stdexec::set_stopped_t()>;

// 一个"具名共享节点"的每帧记忆槽：首次访问时建图并 split，之后复用
class node_ref {
public:
    template <class Fn> node_sender get(Fn&& fn) {
        if (!impl_) {
            if (building_) throw std::logic_error{"frame graph dependency cycle"};
            building_ = true;
            impl_ = wrap(stdexec::split(fn()));   // ← 扇出交给 stdexec
            building_ = false;
        }
        return impl_->subscribe();
    }
    void reset() { impl_.reset(); }               // 每帧调用：单发射，下一帧重建

private:
    struct iface { virtual ~iface() = default; virtual node_sender subscribe() const = 0; };
    template <class S> struct impl final : iface {
        S s;  explicit impl(S x) : s(std::move(x)) {}
        node_sender subscribe() const override { return node_sender{s}; }  // split 后可拷贝
    };
    template <class S> static auto wrap(S s) {
        return std::shared_ptr<iface>{std::make_shared<impl<S>>(std::move(s))};
    }
    std::shared_ptr<iface> impl_;
    bool building_ = false;
};

// 唯一的两个 tuple 工具
auto when_all_tuple(auto&& t) {
    return std::apply([](auto&&... s) { return stdexec::when_all(std::move(s)...); }, t);
}
template <template <class> class Pred> auto filter(auto& t);   // 编译期按 concept 筛选
```

就这些。约 30 行。

**关于类型擦除的成本**：只有扇出节点需要 `node_ref`，而扇出节点无论如何都要过 `split` 的共享 op-state —— 那里已经是间接调用了，`any_sender_of` 再套一层几乎不增加成本。单消费者的节点不用 `node_ref`，`auto` 返回、全静态、零分配。**只有真正扇出的地方付钱**，一帧里通常是 `render.begin`、`render.fence` 和少数几个 compute 节点。

---

## 依赖机制：调另一个 entity 的节点访问器

这是整个设计的核心，也是"静态"的落点：**依赖一个节点 = 持有对方的具体类型，调用它的节点访问器。** 递归调用天然产生拓扑序，`node_ref` 的记忆化保证只执行一次。没有注册表，没有名字解析。

### Renderer

```cpp
class Renderer {
public:
    // 共享节点：多个 entity 依赖它 => node_ref
    node_sender begin_node(World& w) {
        return begin_.get([&] {
            return stdexec::starts_on(w.sched, stdexec::just())
                 | stdexec::then([this] { begin_frame(); });
        });
    }

    node_sender fence_node(World& w) {
        return fence_.get([&] {
            return end_node(w)
                 | stdexec::continues_on(fence_sched_)      // 阻塞等待，独占线程
                 | stdexec::then([this] { wait_fence(); })
                 // 兄弟节点失败会通过 when_all 传播 stop —— fence 必须无条件执行，
                 // 否则 GPU 还在用的资源会被 cleanup 提前释放
                 | stdexec::let_stopped([this] {
                       return stdexec::just() | stdexec::then([this] { wait_fence(); }); });
        });
    }

    node_sender frame_node(World& w) { return fence_node(w); }   // 本 entity 的 sink

    void begin_new_frame() { begin_.reset(); end_.reset(); fence_.reset(); }

private:
    node_sender end_node(World& w) {
        return end_.get([&] {
            return stdexec::when_all(begin_node(w), when_all_tuple(record_nodes(w)))
                 | stdexec::continues_on(w.sched)
                 | stdexec::then([this] { submit(); });
        });
    }

    // barrier：编译期从 entity 列表里挑出所有录制者，取它们的 record 节点
    static auto record_nodes(World& w) {
        return tuple_map(filter<Records>(w.entities),
                         [&](auto& e) { return e.render_record_node(w); });
    }
    node_ref begin_, end_, fence_;
    exec::static_thread_pool fence_sched_{1};
};

template <class E> concept Records =
    requires (E& e, World& w) { e.render_record_node(w); };
```

`render.end` 等所有录制者，就是一个 `when_all` 加一个 concept 筛选。零录制者时 `when_all_tuple({})` = `when_all()`，自动退化成 begin → end。

### 参与渲染的 entity

```cpp
class Terrain {
public:
    explicit Terrain(Renderer& r) : r_(r) {}       // 主动持有依赖的具体类型

    node_sender render_record_node(World& w) {     // 定义这个成员 = 加入 barrier
        return record_.get([&] {
            return stdexec::when_all(compute_node(w), r_.begin_node(w))
                 | stdexec::continues_on(w.sched)
                 | stdexec::then([this] { record(r_.cmd()); });
        });
    }

    auto frame_node(World& w) {                    // sink：单消费者，不用 node_ref
        return r_.fence_node(w)                    // ← 等 fence 后清理，自己声明
             | stdexec::then([this] { release_staging(); });
    }

    void begin_new_frame() { compute_.reset(); record_.reset(); }
private:
    node_sender compute_node(World& w) {
        return compute_.get([&] {
            return stdexec::starts_on(w.sched, stdexec::just())
                 | stdexec::then([this, &w] { compute(w.frame.dt); });
        });
    }
    Renderer& r_;
    node_ref compute_, record_;
};
```

### 纯计算 entity

```cpp
class Physics {
public:
    auto frame_node(World& w) {
        return stdexec::starts_on(w.sched, stdexec::just())
             | stdexec::then([this, &w] { step(w.frame.dt); });
    }
    void begin_new_frame() {}
};
```

没有 `node_ref`、没有类型擦除、没有 render —— 只是一个普通 sender 表达式。约束 3 由"没写"满足。

---

## main

```cpp
int main() {
    exec::static_thread_pool pool{std::thread::hardware_concurrency()};

    Renderer render{device};
    Physics  physics;
    Terrain  terrain{render};
    Audio    audio;

    World world{pool.get_scheduler(), std::tie(render, physics, terrain, audio)};

    while (!world.frame.quit) {
        tuple_for_each(world.entities, [](auto& e) { e.begin_new_frame(); });   // 1. 失效上一帧
        stdexec::sync_wait(                                                     // 2+3. 构图并执行
            when_all_tuple(tuple_map(world.entities,
                                     [&](auto& e) { return e.frame_node(world); }))
        ).value();
        world.frame.advance();                                                  // 4. 循环
    }
}
```

对 entity 完全对称：`begin_new_frame()` + `frame_node(world)`。main 里没有 begin/end/fence，也没有任何两个 entity 之间的连线 —— 图是在 `frame_node` 递归展开的过程中长出来的。

---

## 几点后果

**"构图"和"执行"不再是两个阶段。** `sync_wait` 的参数表达式在求值时就完成了整张图的构造（entity 递归互相调用访问器），`sync_wait` 再启动它。图的结构由 `frame_node` 的可达性决定，所以 entity 必须把自己所有的 sink 放进 `frame_node`（有孤立的 compute 就 `when_all(cleanup, orphan)`）。

**环的诊断变差了。** 类型擦除之后，环从编译错误变成 `node_ref` 里那个 `building_` 标志抛的运行期异常。第一帧就会稳定触发，可以接受；想更早发现就在 debug 构建里跑一次空转帧当自检。

**取消语义要认真对待。** `when_all` 会在一个子节点失败时向兄弟节点请求 stop —— 这是免费得到的好东西（一个 entity 抛异常，整帧其余计算立刻停），但对 fence 和 cleanup 是陷阱：它们必须在取消路径上照样执行。上面 `let_stopped` 那一段就是干这个的，属于 render 的实现细节，其它人不需要知道。

**要流水化很容易。** 把 `sync_wait` 换成 `exec::async_scope::spawn`，就能让第 N+1 帧的 CPU 工作和第 N 帧的 GPU 重叠；`node_ref::reset` 的时机相应改成"上一帧的 scope 空了之后"。图的结构一行都不用改。

---

要我把这版实际写出来吗？`node_ref` + tuple 工具 + 一个 fake renderer + Terrain/Physics 两个 entity 的可编译示例（含那条 fence-before-cleanup 的取消路径测试）。

不应该有begin_new_frame，node_ref.reset这样的状态操作，可以把这部分单独摘出来，比如让entity生成一个xxx，由这个xxx持有状态，本帧完毕下帧销毁。每帧的图要重新构建，确保帧与帧之间的隔离，下一帧不依赖上一帧的状态

# 每帧对象持有图的状态：`Entity → Job`

去掉 `begin_new_frame` / `reset` 之后，责任划分变成：

- **Entity**：长生命周期。只持有资源（device、mesh、ring buffer of command pools）。**没有任何"当前帧"字段。**
- **Job**：entity 每帧生成一个，持有本帧图的全部状态（`node_ref` 记忆槽、本帧的 command buffer、frame slot index）。帧末析构。
- **Frame**：main 每帧构造的一个对象，持有所有 Job 的 tuple。它是 Job 之间**互相查找**的媒介 —— Terrain 的 Job 要的是 Renderer 的 _本帧_ Job，不是 Renderer 本身。

帧隔离于是是**结构性**的，不靠纪律：`Frame` 不可拷贝不可移动，`node_ref` 连 `reset` 都没有（它只在 Job 里构造一次、随 Job 死亡），entity 上没有可被下一帧看见的字段。

> 需要说清一件事：Vulkan 的 per-frame 资源（fence / command pool / semaphore 的 N 槽环形缓冲）**必须**跨帧存活，它归 Renderer 所有。"帧与帧之间隔离"约束的是**图和图的中间状态**，不是资源池 —— Job 只持有"我用第 i 槽"这个索引。

---

## 框架代码（全部）

```cpp
// core/node.hpp
using node_sender = exec::any_sender_of<
    stdexec::set_value_t(), stdexec::set_error_t(std::exception_ptr), stdexec::set_stopped_t()>;

// 具名共享节点的记忆槽。无 reset —— 生命周期就是 Job 的生命周期。
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
        node_sender subscribe() const override { return node_sender{s}; }
    };
    template <class S> static std::shared_ptr<iface> wrap(S s) {
        return std::make_shared<impl<S>>(std::move(s));
    }
    std::shared_ptr<iface> impl_;
    bool building_ = false;
};
```

```cpp
// core/frame.hpp
template <class... Es> struct World {              // 长生命周期
    stdexec::scheduler auto sched;
    std::tuple<Es&...> entities;
    FrameInfo info;                                 // dt、帧号、输入快照
};

template <class... Es>
class Frame {                                       // 每帧一个，帧末析构
public:
    explicit Frame(World<Es...>& w)
        : w_(w), jobs_(std::get<Es&>(w.entities).begin_frame(w)...) {}

    Frame(const Frame&) = delete;
    Frame(Frame&&) = delete;                        // 结构性地防止逃出本帧

    World<Es...>& world() { return w_; }

    template <class E> auto& job() {
        static_assert((std::is_same_v<E, Es> || ...),
                      "依赖了一个 main 没有收集的 entity");
        return std::get<index_of_v<E, Es...>>(jobs_);
    }

    // 供 barrier 使用：对满足 Pred 的 Job 调 fn，结果收成 tuple
    template <template <class, class> class Pred, class Fn>
    auto collect(Fn&& fn) {
        return std::apply([&](auto&... j) {
            return std::tuple_cat(collect_one<Pred>(j, fn)...);
        }, jobs_);
    }

    void run() {                                     // 构图 + 执行，sender 不逃出本函数
        stdexec::sync_wait(when_all_tuple(
            collect<always>([this](auto& j) { return j.frame_node(*this); }))).value();
    }
private:
    World<Es...>& w_;
    std::tuple<job_t<Es, World<Es...>>...> jobs_;
};
```

加上 `when_all_tuple` / `index_of_v` / `collect_one` 三个小工具。**没有 GraphBuilder、没有 Tag、没有拓扑排序** —— 图仍然是 sender 表达式，`when_all` 汇聚、`split` 扇出、表达式嵌套即拓扑序。

Job 的节点访问器对 `Frame` 类型取模板参数，避免 `Terrain.hpp` ↔ `Frame.hpp` 的头文件循环。

---

## Renderer

```cpp
class Renderer {                                   // 长生命周期：device + N 槽资源环
public:
    class Job {
    public:
        Job(Renderer& r, World_& w) : r_(r), w_(w), slot_(w.info.index % Renderer::kSlots) {}

        node_sender begin_node(auto& f) {          // 共享 => node_ref
            return begin_.get([&] {
                return stdexec::starts_on(f.world().sched, stdexec::just())
                     | stdexec::then([this] { cmd_ = r_.begin_frame(slot_); });
            });
        }

        node_sender fence_node(auto& f) {
            return fence_.get([&] {
                return end_node(f)
                     | stdexec::continues_on(r_.fence_sched())     // 阻塞等待，独占线程
                     | stdexec::then([this] { r_.wait_fence(slot_); })
                     // when_all 会在兄弟节点失败时请求 stop；fence 必须无条件执行，
                     // 否则 cleanup 会在 GPU 用完之前释放资源
                     | stdexec::let_stopped([this] {
                           return stdexec::just()
                                | stdexec::then([this] { r_.wait_fence(slot_); }); });
            });
        }

        node_sender frame_node(auto& f) { return fence_node(f); }   // 本 Job 的 sink
        VkCommandBuffer cmd() const { return cmd_; }                // 给录制者用

    private:
        node_sender end_node(auto& f) {
            return end_.get([&] {
                return stdexec::when_all(begin_node(f), when_all_tuple(record_nodes(f)))
                     | stdexec::continues_on(f.world().sched)
                     | stdexec::then([this] { r_.submit(slot_); });
            });
        }
        // barrier：编译期挑出本帧所有录制者的 record 节点
        static auto record_nodes(auto& f) {
            return f.template collect<Records>([&](auto& j) { return j.render_record_node(f); });
        }

        Renderer& r_;  World_& w_;  std::uint32_t slot_;
        VkCommandBuffer cmd_{};                     // 本帧数据，帧末随 Job 消失
        node_ref begin_, end_, fence_;
    };

    Job begin_frame(World_& w) { return Job{*this, w}; }
};

// "这个 Job 参与录制" 的判据
template <class J, class F> concept Records =
    requires (J& j, F& f) { j.render_record_node(f); };
```

## 参与渲染的 entity

```cpp
class Terrain {
public:
    explicit Terrain(Renderer& r) : r_(r) {}        // 主动持有依赖的具体类型

    class Job {
    public:
        Job(Terrain& t, World_& w) : t_(t), w_(w) {}

        node_sender render_record_node(auto& f) {   // 定义此成员 = 加入 render 的 barrier
            return record_.get([&] {
                auto& rj = f.template job<Renderer>();        // ← 本帧的 Renderer Job
                return stdexec::when_all(compute_node(f), rj.begin_node(f))
                     | stdexec::continues_on(f.world().sched)
                     | stdexec::then([this, &rj] { staging_ = t_.record(rj.cmd()); });
            });
        }

        auto frame_node(auto& f) {                  // sink：单消费者，不需要 node_ref
            return f.template job<Renderer>().fence_node(f)      // ← 等 fence 后清理
                 | stdexec::then([this] { t_.release(staging_); });
        }
    private:
        node_sender compute_node(auto& f) {
            return compute_.get([&] {
                return stdexec::starts_on(f.world().sched, stdexec::just())
                     | stdexec::then([this] { mesh_ = t_.compute(w_.info.dt); });
            });
        }
        Terrain& t_;  World_& w_;
        Mesh mesh_{};  StagingHandle staging_{};    // 本帧数据
        node_ref compute_, record_;
    };

    Job begin_frame(World_& w) { return Job{*this, w}; }
private:
    Renderer& r_;
};
```

## 纯计算 entity

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

没有 `node_ref`、没有类型擦除、没有 render，全静态零分配。

---

## main

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

循环体三行，没有任何状态复位调用 —— 复位就是 `frame` 出作用域。

---

## 这一版换来的性质

**帧隔离是编译器强制的，不是约定。** `Frame` 删掉了拷贝和移动构造，所以图和 Job 无法逃出 `while` 的一次迭代；`node_ref` 没有 `reset`，所以不存在"忘了复位导致沿用上一帧 split 状态"这个 bug 类别；entity 上没有帧字段，所以"上一帧的中间结果"物理上不存在。

**依赖错误变成编译错误。** `f.job<Renderer>()` 的 `static_assert` 会在 main 忘记收集某个 entity 时直接报出来，而不是运行到某帧才崩。

**Renderer::Job 顺便成了"本帧渲染上下文"。** `cmd_`、`slot_` 这些本来最容易被写成 Renderer 成员、然后在多线程录制里踩到的东西，现在天然是每帧独立对象的成员。这是把 `begin_new_frame` 摘出来之后附带拿到的最大好处。

**要流水化时把 Frame 移进异步操作。** `Frame` 不可移动会拦住这条路，所以真要做 CPU/GPU 重叠时改成 `std::unique_ptr<Frame>` 交给 `exec::async_scope`：

```cpp
scope.spawn(stdexec::just(std::make_unique<Frame>(world))
          | stdexec::let_value([](auto& f) { return f->sender(); }));
```

帧对象活到该帧的异步操作结束为止，仍然没有跨帧共享状态。图的结构一行不改。

---

要我把这版实际写出来吗？`node_ref` / `Frame` / 三个 tuple 工具 + fake renderer + Terrain/Physics 的可编译示例，外加一个测试：让 Physics 抛异常，验证 fence 和 cleanup 仍然按序执行。

render实现为动态找出本帧录制者的record节点 用 let_value 把"读名单"从构图期推迟到启动期 先说清问题在哪：如果 render.end 在构建时读取录制者名单，就产生了构建顺序依赖 —— Frame::run 遍历 job tuple 调 frame_node，谁先谁后不确定。只要有一个录制者的 frame_node 排在 Renderer 后面，它就会被漏掉。而且静态的 collect<Records> 还有两个表达不了的东西：本帧条件性录制（被剔除了就不录），和插件。 关键观察是：stdexec 的惰性模型已经免费给了我们一条分界线 —— 整张图在 sync_wait 连接之前就已经完全构建完毕。所以只要让名单在启动期才被读取，构建顺序就彻底无关了。而"把一个 sender 的构建推迟到启动之后"正是 let_value 的语义。

# 把名单读取推到启动期

先校准一句问题的边界：静态 `collect<Records>` 是 **pull**（render 遍历整个 job tuple 主动取），所以它本身不受构建顺序影响 —— 谁先构建，`collect` 都会扫到全部成员。真正有顺序依赖的是 **push**（录制者自己往名单里塞）。但你要的正是 push，因为插件和"本帧被剔除就根本不注册"这两件事 pull 表达不了（前者名单不在 `Es...` 里，后者只能退化成注册一个 `just()` 空节点）。切到 push 之后，构建顺序依赖是实打实的，`let_value` 也确实是正确的解法。

## 分界线在哪

`sync_wait(expr)` 的执行严格分两段：

```
        connect(expr)                    start(op)
  ───────────────┼──────────────────────────┼──────────────────────►
   构建期：所有 frame_node 被调用            启动期：begin 完成后
           所有 push 发生                    let_value 的函数体才运行
```

`let_value` 的函数体在**前驱完成后**才执行，而前驱要完成必须先 `start`，`start` 又必然在整棵表达式树 `connect` 完之后。所以只要名单在 `let_value` 里读，它一定看得见构建期的全部注册 —— 不管注册发生在遍历 job tuple 的第几个。

## 名单本身

```cpp
// render/recorder.hpp —— 只有录制者和插件 include
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

`sealed_` 是关键：推迟到启动期读取之后，"漏掉一个录制者"从静默丢帧内容变成**一次明确的异常**。没有这个标志，这套机制的失败模式是不可见的。

## render.end

```cpp
node_sender Renderer::Job::end_node(auto& f) {
    return end_.get([&] {
        return begin_node(f)
             | stdexec::let_value([this] {
                   // ── 启动期 ── 全图已 connect 完毕，名单已齐
                   subscribing_ = true;
                   for (auto& s : roster_.seal())
                       scope_.spawn(std::move(s) | stdexec::upon_error(
                           [this](std::exception_ptr e) { errors_.capture(std::move(e)); }));
                   subscribing_ = false;
                   return scope_.on_empty();          // 动态 fan-in
               })
             | stdexec::continues_on(sched_)
             | stdexec::then([this] { errors_.rethrow_if_any(); r_.submit(slot_); });
    });
}
```

## 录制者

```cpp
auto Terrain::Job::frame_node(auto& f) {
    auto& rj = f.template job<Renderer>();
    if (t_.visible())                              // 本帧被剔除 => 根本不注册
        rj.recorders().add(record_node(f, rj));    // 构建期 push，顺序无关
    return rj.fence_node(f) | stdexec::then([this] { t_.release(staging_); });
}

node_sender Terrain::Job::record_node(auto& f, auto& rj) {   // 单消费者，不需要 split
    return stdexec::when_all(compute_node(f), rj.begin_node(f))
         | stdexec::continues_on(f.world().sched)
         | stdexec::then([this, &rj] { staging_ = t_.record(rj.cmd()); });
}
```

被剔除时连 `compute_node` 都不会被构建 —— 这是 pull 模型给不了的。

---

## 三个必须一起处理的后果

**动态数量的 fan-in 没有 `when_all` 可用。** `when_all` 是变参的，名单是运行期长度。`exec::async_scope` + `on_empty()` 是现成的动态汇聚，代价是 `spawn` 要求 sender 以 `set_value()` 完成 —— 错误会 terminate。所以每个录制节点必须先 `upon_error` 把异常收进 `errors_`，在 `then(submit)` 里统一重抛。副作用是**错误不再自动取消兄弟录制节点**（`when_all` 原来免费给的），想要 fail-fast 得在 `capture` 里加一句 `scope_.request_stop()`。

**订阅一个已经完成的 `split` 是安全的。** `let_value` 的函数体运行时，`begin_node` 已经完成了；而每个录制节点内部又 `when_all(..., rj.begin_node(f))`。这看着像自引用，实际没问题：`split` 支持晚订阅，对已完成的节点会立即投递缓存的结果。但它是**同步就地投递**的 —— 录制节点会在 `begin` 的完成线程上原地开始跑。所以每个录制节点里那句 `continues_on(w.sched)` 不是可选的，去掉它就会把整个录制串行到一条线程上。

**环从"构建期可查"退化成"启动期死锁"。** 这是这次改动唯一真正的损失。原来 `node_ref::building_` 能在构建时抓到环；现在如果某个录制节点错误地依赖了 `fence_node` 或 `end_node`，构建期一切正常，到启动期它会去订阅一个**正在执行中**的 `split`（我们此刻就在它的 `let_value` 里），于是挂进等待队列永不返回 —— 一次静默死锁。上面那个 `subscribing_` 标志就是最低成本的检测：

```cpp
node_sender Renderer::Job::fence_node(auto& f) {
    if (subscribing_) throw std::logic_error{"录制节点依赖了 fence/end —— 环"};
    ...
}
```

因为所有订阅都发生在 `spawn` 的 `connect` 里，也就是那个循环内部，标志的作用域是准的。只有录制节点内部再套一层 `let_value` 延迟订阅时会漏掉 —— 剩下的靠"第一帧必现"兜底。

---

## 顺带得到的东西

这条分界线不止能用来读名单。任何**需要全图信息**的决策都可以用同样的手法推迟：本帧实际录制者数量决定用几个 secondary command buffer、按 pipeline 排序后再提交、根据注册进来的 pass 数动态选 render pass 变体 —— 全都写在同一个 `let_value` 里，因为那一刻构建已经结束、执行刚刚开始。

插件的接线也顺势解决了：插件拿不到 `Frame<Es...>` 这个类型，但 `render::roster&` 是具体类型。给 `Renderer::Job` 加一个抽象接口 `render::frame_ctx { roster& recorders(); node_sender begin(); node_sender fence(); VkCommandBuffer cmd(); }`，插件只 include `render/recorder.hpp`，编译期完全不需要知道 main 那份 entity 列表。

要我把这版写成可编译的示例吗？重点覆盖三个用例：注册顺序打乱（Renderer 排在 job tuple 第一个 vs 最后一个，结果一致）、录制者本帧被剔除、以及一个故意制造的环触发 `subscribing_` 检测。

render end部分应该实现一个动态when_all而不是一个一个spawn

# 动态 `when_all`

`async_scope::spawn` + `on_empty()` 在这里是四重错误：

1. **错误不传播** —— `spawn` 要求子操作以 `set_value()` 完成，错误直接 terminate，所以必须手动 `upon_error` 收集再重抛。`end_node` 的失败信号本该沿 fence → cleanup → 顶层 `when_all` 自然传出去。
2. **stop 不传播** —— 上层取消整帧传不到录制节点；某个录制节点失败也不会取消其余录制节点。`when_all` 这两条都是免费的。
3. **每个 spawn 一次分配** —— 而 join 只需要一个 op-state 数组。
4. **语义不对** —— `on_empty()` 是"scope 空了"，不是"这些操作都完成了"。它不携带子操作的完成信号，只是恰好在数量归零时触发。

而且写一个真正的动态 `when_all` 比想象中便宜，**因为名单是同质且无值的**（`std::vector<node_sender>`，完成签名固定）。前面接受的类型擦除在这里回本了：不需要 `transform_completion_signatures`、不需要 value tuple 的 variant 折叠，完成签名可以直接写死。

> 先确认一下你手上 stdexec 的版本有没有自带 `when_all_vector` / `when_all_range` 之类的范围版本；有就直接用。下面这份是没有时的最小实现。

## `when_all_range`

```cpp
// core/when_all_range.hpp
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

    void set_value() noexcept                    { op->arrive(); }
    void set_error(std::exception_ptr e) noexcept { op->fail(std::move(e)); op->arrive(); }
    void set_stopped() noexcept                   { op->stopped();          op->arrive(); }
    auto get_env() const noexcept {              // 子节点看到的是本 op 的 stop token
        return stdexec::prop{stdexec::get_stop_token, op->stop_.get_token()};
    }
};

template <class S, class R>
struct range_op {
    using child = child_rcvr<S, R>;
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
        case 0:  stdexec::set_value(std::move(rcvr_));                      break;
        case 1:  stdexec::set_error(std::move(rcvr_), std::move(err_));     break;
        default: stdexec::set_stopped(std::move(rcvr_));                    break;
        }
    }

    struct fwd_stop { range_op* op; void operator()() noexcept { op->stop_.request_stop(); } };
    using stop_cb = stdexec::stop_token_of_t<stdexec::env_of_t<R>>
                        ::template callback_type<fwd_stop>;

    R rcvr_;
    std::vector<S> src_;
    std::size_t n_;
    std::unique_ptr<std::optional<child_op_t>[]> ops_;   // 数组不重分配 => 可容纳不可移动类型
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

三个容易踩的点，都在上面处理了：

- **`connect` 全部做完再 `start`**。否则第一个同步完成的子节点可能在后面的子节点还没 connect 时就把整个 op 完成掉（进而析构掉尚未 connect 的存储）。
- **`pending_` 预置为 `n + 1`**。启动循环自己占一份，所以循环体内不可能触发完成和析构 —— 循环末尾 `arrive()` 才交还。少了这一手，全部子节点同步完成时会在循环中间析构 `this`。
- **`unique_ptr<optional<T>[]>` + `conv`**。`connect_result_t` 不可移动，所以既不能用 `vector`（增长要求可移动），也不能用 `optional::emplace(连接结果)`（那是移动构造）。定长数组 + 转换运算符就地构造是唯一干净的组合。

## 接回 render.end

```cpp
node_sender Renderer::Job::end_node(auto& f) {
    return end_.get([&] {
        return begin_node(f)
             | stdexec::let_value([this] {
                   phase_.store(phase::recording);
                   return fg::when_all_range(roster_.seal());   // 只 move vector，不 connect
               })
             | stdexec::continues_on(sched_)
             | stdexec::then([this] { phase_.store(phase::submitting); r_.submit(slot_); });
    });
}
```

`errors_` 收集槽和 `rethrow_if_any()` 一起删掉：录制节点抛出的第一个异常现在是 `end_node` 的 `set_error`，顺着 fence → cleanup → 顶层 `when_all` 出去，`sync_wait().value()` 直接把它重抛给 main。

## 顺带修好的环检测

上一版的 `subscribing_` 标志本来已经失效了 —— `let_value` 是在函数返回**之后**才 connect 返回的 sender，标志括不住订阅时刻。改成 `phase_` 之后覆盖反而更好：

```cpp
node_sender Renderer::Job::fence_node(auto& f) {
    if (phase_.load() == phase::recording)
        throw std::logic_error{"录制节点依赖了 fence/end —— 环"};
    ...
}
```

`phase_` 由 `end_node` 的两个节点翻转，覆盖整个录制窗口，所以连录制节点内部再套一层 `let_value` 的延迟订阅也能抓到。清理节点在构建期调 `fence_node`，那时 `phase_` 还是 `constructing`，不受影响。

## 两点后续

**fence 的 `let_stopped` 现在更重要了。** stop 真的会传播了：某个录制节点失败 → `when_all_range` 取消其余录制者 → `end_node` 以 error 完成 → fence 收到的是 error 而不是 value。所以 fence 除了 `let_stopped` 还要 `let_error`，两条路径都必须落到 `wait_fence(slot_)`，否则一次录制异常就会漏掉 fence 等待，而 cleanup 又已经被 error 跳过 —— 资源泄漏加下一帧复用正在被 GPU 读的 slot。这是整套设计里最需要写测试的一处。

**`make_unique` 换成帧 arena。** 一帧一次分配已经可以接受，但既然 `Frame` 的生命周期就是一帧，把 `ops_` 和 `roster_` 的 vector 都挂到帧 arena 上是顺手的事，`range_op` 加一个 allocator 参数即可。

要我把 `when_all_range` 和那条 fence 的 error/stopped 双路径写成带测试的可编译版本吗？测试覆盖：空名单、单个录制者抛异常时其余被取消且 fence 仍执行、上层取消整帧、以及注册顺序打乱结果一致。

能不能做到把entities列表也做成动态的

能。而且分界线很干净：**依赖边保持静态，只有"谁参与本帧"变成动态**。

`World<Es...>` 现在把两件事捏在一起了：

1. main 顶层要 join 的 frame_node 名单 —— 这个可以完全动态
2. `f.job<E>()` 的依赖查找 —— 这个不需要 tuple，因为**entity 早就持有对端的具体引用了**（`Terrain{render}` 里有 `Renderer&`）

第 2 点是关键。`job<E>()` 存在的唯一理由是 Job 是每帧新建的，Terrain::Job 要的是 Renderer::Job 而不是 Renderer。那就让查找**走 entity 自己**：Renderer 暴露"本帧的我"，Terrain 通过手里那个 `Renderer&` 拿。tuple 就没用了。

## 被依赖的 entity 加一个槽

```cpp
// 只有"会被别人依赖"的 entity 需要它。纯计算 entity 一行都不用改。
template <class Job>
class depended_on {
public:
    Job& job() const {                       // 强依赖
        if (!j_) throw std::logic_error{"依赖的 entity 未参与本帧"};
        return *j_;
    }
    Job* job_if() const noexcept { return j_; }   // 弱依赖：不在就跳过
private:
    template <class> friend class entity_slot;
    Job* j_ = nullptr;
};

class Renderer : public depended_on<Renderer::Job> { /* ... */ };
```

## 动态名单

```cpp
struct IEntity {
    virtual ~IEntity() = default;
    virtual void        begin_frame(World&)   = 0;   // 阶段 A：建 Job
    virtual node_sender frame_node(Frame&)    = 0;   // 阶段 B：构图
    virtual void        end_frame() noexcept  = 0;
};

template <class E>
class entity_slot final : public IEntity {           // entity 自己不继承任何东西
public:
    explicit entity_slot(E& e) : e_(e) {}
    void begin_frame(World& w) override {
        job_.emplace(conv{[&] { return e_.begin_frame(w); }});
        if constexpr (has_slot_v<E>) e_.j_ = &*job_;
    }
    node_sender frame_node(Frame& f) override { return job_->frame_node(f); }
    void end_frame() noexcept override {
        if constexpr (has_slot_v<E>) e_.j_ = nullptr;  // 帧后再取 => 抛异常而非悬垂
        job_.reset();
    }
private:
    E& e_;
    std::optional<job_t<E>> job_;
};

class Frame {
public:
    explicit Frame(World& w) : w_(w) {
        for (auto& e : w_.entities) e->begin_frame(w_);          // A：全部建完
    }
    ~Frame() {
        for (auto it = w_.entities.rbegin(); it != w_.entities.rend(); ++it)
            (*it)->end_frame();                                   // 逆序销毁
    }
    Frame(const Frame&) = delete;
    Frame(Frame&&)      = delete;

    World& world() { return w_; }

    void run() {
        std::vector<node_sender> roots;
        roots.reserve(w_.entities.size());
        for (auto& e : w_.entities) roots.push_back(e->frame_node(*this));  // B
        stdexec::sync_wait(fg::when_all_range(std::move(roots))).value();
    }
private:
    World& w_;
};
```

阶段 A / 阶段 B 分离是原设计里**已经有**的（Frame 构造建全部 Job，`run()` 才调 frame_node），所以 `render.job()` 在任何 `frame_node` 里都必然有效——注册顺序不影响它。规则照旧：**Job 构造函数里不许访问别的 Job**。

`Terrain::Job` 的改动只有一行：

```cpp
node_sender frame_node(Frame& f) {
    auto& rj = t_.renderer().job();          // 原来是 f.template job<Renderer>()
    if (t_.visible()) rj.recorders().add(record_node(f, rj));
    return rj.fence_node(f) | stdexec::then([this] { t_.release(staging_); });
}
```

`main`：

```cpp
World world{pool.get_scheduler(), {}};
world.add(render); world.add(physics); world.add(terrain); world.add(audio);
for (auto& p : plugins) world.add(*p);        // dlopen 来的也一样
while (!world.info.quit) { Frame frame{world}; frame.run(); world.info.advance(); }
```

## 三点顺带的收益

- `frame_node` / `begin_frame` 签名变成**具体类型**（`Frame&`、`World&`），不再是 `auto&` 模板 —— 因为 Frame 不再依赖 `Es...`。这才是能虚化的前提，也让 entity 头文件互相不用见到对方的模板实参。
- `when_all_range` 从 render.end 的专用件变成顶层 join 也在用的东西，一份代码两处用。
- 中途增删 entity 只在**帧与帧之间**（`Frame` 析构后、下一个 `Frame` 构造前）安全，这正好和"每帧重建图"对齐，不需要额外的锁或延迟队列。

## 代价，说清楚

|丢掉的|变成什么|
|---|---|
|`static_assert("依赖了一个 main 没有收集的 entity")`|运行时 `job()` 抛异常 / 或用 `job_if()` 显式处理缺席|
|纯计算 entity 的根节点保持具体类型|每 entity 每帧一次 `any_sender_of` 擦除（一次分配 + 间接调用）。只在**根**，内部节点仍全具体|
|tuple 的编译期去重|同一 entity 注册两次要靠 `add()` 里查重|

第一行是唯一真正的损失。如果 Renderer / Physics 这类"必然存在"的核心 entity 你不想失去编译期检查，可以混合：`World` 同时持有一个静态 tuple（核心，`job<E>()` 带 static_assert）和一个动态 vector（插件，走 `IEntity`），`run()` 把两边的根节点拼进同一个 `vector<node_sender>`。我倾向这个 —— 动态是为插件付的钱，没必要让核心 entity 也付。

要我把这一节写进 [frame-graph-design.md](app://localhost/epitaxy/tmp/frame-graph-design.md) 吗（作为 §10，或者直接改写 §4/§5 让动态成为主线）？