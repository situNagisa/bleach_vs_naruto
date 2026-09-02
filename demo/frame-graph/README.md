# frame-graph

每帧重建的任务图。**entity 名单是动态的，依赖边是静态的。**

这个 demo 落地 [tmp/frame-graph-design.md](../../tmp/frame-graph-design.md) 里的架构，并把它从
"main 持有 `std::tuple<Es&...>`" 推进到 "main 持有 `std::vector<unique_ptr<basic_entity_slot>>`"——
插件能在运行期进出，而 terrain 依赖 renderer 这条边仍然是具体类型直连，不退化成 tag 查表。

## 分工

| 角色 | 只需要知道 |
| --- | --- |
| main | 收集 entity（`world::add`）、构 frame、跑 frame、循环 |
| 参与渲染的 entity 开发者 | 手里那个 `renderer&`；本帧的 renderer job 从它的 `_job_slot` 取 |
| 纯计算 entity 开发者 | `frame_context`（调度器 + dt + 帧号）。**没有任何 render 概念** |

`static_assert(!exposes_current_job<physics>)` 就是第三行那句话的机器检查：纯计算 entity 里没有
`node_ref`、没有类型擦除、不 include `renderer.h`。

## 关键点

**图就是 sender 表达式。** 这一层不做拓扑排序、不管前驱计数——扇出交给 `split`、扇入交给
`when_all` / `when_all_range`、先后由表达式嵌套表达。`frame_graph.h` 只补 stdexec 缺的三件小东西：

| 补丁 | 干什么 |
| --- | --- |
| `node_ref` | 具名共享节点的备忘格。第一次 `get` 建图并套 `split`，之后都是订阅 |
| `node_roster` | 启动期才读取的动态汇合点。构建期往里挂，谁先挂谁后挂无所谓 |
| `when_all_range` | 元素个数运行期才知道的扇入 |

**两阶段。** `frame` 构造时先让每个 entity 生成本帧 job（阶段 A），`frame::run` 才向每个 job 要根节点
（阶段 B）。阶段 A 全部做完才进阶段 B，所以阶段 B 里任何 job 都能拿到任何别的 job——
**注册顺序无关**（场景 1）。唯一约定：**job 的构造函数里不许访问别的 job**。

**名单在启动期才读。** `render.end` 不在构图时读录制名单，而是把它包在 `let_value` 里：
stdexec 的惰性模型免费给了一条分界线——整张图在 `connect` 之前就已经构建完毕，
于是"读名单"落在分界线之后，构建顺序彻底无关，条件性录制（场景 2）和插件（场景 8）也就成立了。

**帧隔离是结构性的。** 每帧的状态全在 job 上，`frame` 析构时逆序销毁；`frame` 既不可拷贝也不可移动，
`node_ref` 没有 `reset`。没有"清空上一帧"这种操作，因为上一帧的东西已经不存在了。

## 类型擦除：用 `exec::any_sender`，外加一层 `env_gate_sender`

`node_sender` 就是 `::exec::any_sender<node_receiver>`。

（本文档早先声称"`any_sender_of` 丢掉接收者环境里的查询，停止令牌过不去"，因此手写了一套擦除。
**那个说法是错的**，已删。`any_receiver` 的第二个模板参数就是要穿过擦除边界的查询清单；
声明一条 `get_stop_token` 之后，`any_sender_of.hpp` 里的 `_state<Receiver, inplace_stop_token>`
会自带一个 `inplace_stop_source` 把外层任意类型的令牌桥接进来，外层令牌本身兼容时还走零开销特化。）

唯一自造的是 `env_gate_sender`——薄薄一层，把被擦除表达式看到的外层环境钉成固定的 `node_env`。
它是**上游 bug 的规避**，不是重写擦除：

- `node_receiver` 的环境是那个多态接口本身（`_interface_::get_env()` 返回 `_interface_ const&`），
  继承自 `__any::__interface_base`，拷贝构造 deleted，因此不可移动。
- `__continues_on.hpp:228` 用 `__fwd_env_t<_Env>`（按**值**）转发环境，落到
  `__env::__fwd<_Env>` 的 `static_assert(__nothrow_move_constructible<_Env>)` 上直接炸。
- 于是"带查询的 `any_sender` + 任何含 `starts_on` / `continues_on` 的表达式"编译不过。
- 本地 pin 的 `f91f6363` 和上游 HEAD `4754c76d`（新 27 个提交）都还带着它。上游自己的
  `test/exec/test_any_sender.cpp:798` 用的是完全相同的写法，只是它擦除的是 `just(42)`，
  从没擦过调度 sender，所以这个洞没被测到。
- 在副本上把 `__fwd_env_t<_Env>` 改成 `__fwd_env_t<_Env const&>` 即可修复——但那要改 stdexec，
  所以这里选了不侵入依赖的一层。

`env_gate_sender` 的完成签名是写死的，`any_sender` 也就不必再拿那个不可移动的环境去递归推导孩子
的签名；停止令牌照旧穿过去（库把外层令牌桥接成 `inplace_stop_token`，这层再把它读进 `node_env`）。

## 为什么不逐个 `spawn` 进 `async_scope`

`scope.spawn` 丢错误、丢取消、每个孩子一次堆分配，而 `scope.on_empty()` 是"空了"不是"汇合"。
`when_all_range` 是一个真正的汇合子：一个倒计数器 + 首个非正常完成胜出 + fail-fast 广播取消。
两处细节值得单独记住：

- **先全部 `connect`，再全部 `start`。** 反过来的话，第一个孩子同步完成时后面的孩子还没连上。
- **`_pending` 初值是 `n + 1`。** 那多出来的 1 留给启动循环自己，否则某个同步完成的孩子会在循环
  还没走完时把计数减到 0，触发完成、进而销毁 op-state，后面的迭代踩在死对象上。

## 三条完成路径都要落到 fence

`fence_node` 是 `let_error` + `let_stopped` + `then` 三条路各接一次 `wait_fence`。
少任何一条，这个 slot 的 GPU 资源就没人回收，下一轮复用时踩在 GPU 还在读的内存上。
demo 里 `renderer::open_command_buffer` 对 `_slot_busy` 断言，就是为了让"漏等 fence"当场炸掉，
而不是变成偶发的资源复用 bug。场景 4（录制失败）和场景 5（整帧取消）各盯一条非正常路径。

注意 `continues_on` 只搬运 value 这条路；error / stopped 绕过调度切换，直接在出事的那根线程上跑
`let_error` / `let_stopped`。对 fence 等待来说可以接受，但换成别的收尾动作时要重新算一遍。

## 取消不走结构边

`::exec::split` 在**订阅者的停止令牌已经停止**时，直接 `set_stopped`，**根本不启动共享体**
（合理：一个已被取消的订阅者不该强行触发共享工作）。而 fence 节点本身就是个 `split`——
如果取消沿着图的结构边往下压，整帧被取消时 fence 的清理体一次都不会跑，slot 就漏了。
最初这条正是场景 5 唯一挂掉的断言：整帧取消后一条事件都没有，连 `begin` 都没有。

所以这里把两件事拆开：

- **结构边永不取消。** `frame::run` 给根接收者的是 `_structural_source` 的令牌，
  这个源从不 `request_stop`。图的骨架因此不会在订阅侧被 `split` 短路。
- **取消由干活的节点自己领。** 外部令牌转发进 `frame::_stop_source`，节点用
  `cancellable(sender, context)`（`write_env` 注入 `get_stop_token`）显式接上它。
  目前只有 `renderer::job::begin_node` 用了——begin 不跑 ⇒ 没有录制 ⇒ 没有提交，
  而 end / fence 仍沿 stopped 路走完，fence 照等。

注入而不是继承，代价是"想响应取消就必须说出来"；换来的是取消不会顺手掐死清理路径。

## 录制节点里的 `continues_on` 不是可选的

名单是启动期读的，读的时候 `begin` 早就完成了——订阅一个已完成的 `split` 会**原地同步派发**。
不换一次调度的话，所有录制者会串在同一根线程上跑完。见 `entities.h` 里 `terrain::job::record_node`。

## 动态名单的代价

| 丢掉的 | 变成什么 |
| --- | --- |
| `static_assert("依赖了一个 main 没有收集的 entity")` | 运行期 `job_slot::get()` 抛异常（场景 8 末尾）；可选依赖用 `get_if()` |
| 纯计算 entity 的根节点保持具体类型 | 每 entity 每帧一次类型擦除（一次分配 + 一次间接调用）。只在**根**，内部节点仍全具体 |
| tuple 的编译期去重 | `world::add` 用地址线性查重，断言拦住重复注册 |

第一行是唯一真正的损失。核心 entity 如果不想失去编译期检查，可以混合：`world` 同时持有一个静态
tuple（核心，带 `static_assert`）和这个动态 vector（插件），`run` 把两边的根节点拼进同一个
`vector<node_sender>`。这个 demo 只做动态那一半。

## 已知边界

**构建期的环能抓到，运行期的环抓不到。** `node_ref::get` 在建图时用 `_building` 标记检测自指，
`renderer::job::recorders()` 用 `_phase` 拦截封存后的注册（场景 6）。但"某个录制节点在构建期就
依赖了 `fence_node()`"是一个真正的运行期环——`render.end` 等录制、录制等 fence、fence 等
`render.end`——它不会触发上面任何一个检查，表现为启动后静默死锁。当前没有对策，只有约定：
**录制节点只许依赖 `begin_node()` 和自己的计算节点。**

## 场景

| # | 场景 | 钉住的结论 |
| --- | --- | --- |
| 1 | 注册顺序无关 | renderer 排第一 vs 排最后，录制者集合一致 |
| 2 | 条件性录制 | 被剔除的 entity 连计算节点都不构建，render.end 照常提交 |
| 3 | 空名单 | `when_all_range({})` 立即完成，begin 直连 end |
| 4 | 录制失败 | 异常传到 main，提交跳过，**fence 仍执行**，下游清理不误跑 |
| 5 | 整帧取消 | 上层 `request_stop`，**fence 仍执行**，slot 不留占用态 |
| 6 | 迟到注册 | 名单封存后再注册是响亮的异常，不是静默丢失 |
| 7 | 帧间隔离 | 连续 5 帧各一套全新 job，slot 按 3 格环轮转 |
| 8 | 动态名单 | 帧间增删 entity 立刻生效；依赖缺席时当场点名 |

## 构建

跟随主工程（`BVN_BUILD_DEMOS=ON`，默认开）：

```bash
cmake --build build --target bvn_frame_graph_demo
```

或独立构建（`NAGISA_LIBRARY_ROOT` 指向放 stdexec 的目录，或直接给 `FRAME_GRAPH_STDEXEC_ROOT`）：

```bash
cmake -S demo/frame-graph -B build/frame-graph -DFRAME_GRAPH_STDEXEC_ROOT=/path/to/stdexec
```

## 文件

| 文件 | 内容 |
| --- | --- |
| `frame_graph.h` | 框架：`manual_lifetime` / `node_sender` / `node_ref` / `node_roster` / `when_all_range` / `job_slot` / `entity_slot` / `world` / `frame` |
| `renderer.h` | 假 renderer entity：begin → 收录制 → submit → 帧末等 fence，外加事件流与 slot 占用断言 |
| `entities.h` | 三类 entity 开发者：`physics`（纯计算）、`terrain`（参与渲染）、`overlay`（带故障开关） |
| `main.cpp` | 八个场景 |
