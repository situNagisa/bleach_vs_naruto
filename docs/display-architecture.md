# bvn 显示架构（Display Architecture）· 设计方向

> 日期：2026-07-01　状态：**方向已定**
> 本文记录「显示架构」的设计思想，**用于描述项目、非实现计划**。它把 `engine-spec.md §4.5`（英雄产出渲染任务）的思路，明确成一套独立的显示架构。

---

## 1. 核心哲学：一切可视物自己画自己

**引擎不建模"场景"。** 任何想出现在屏幕上的东西，都是一个 **`renderable`**——它知道**怎么把自己画进每一帧**。

**`render(renderer)` 是什么**：它返回一个 sender（协程），代表 `t` **参与渲染的整个生命周期**——`t` 自己在里面完成 初始化 → 逐帧录制循环 → 收尾。渲染运行时只把它**启动（spawn）一次**，之后它自己每帧录一笔；它**不是**"每帧被外部调用一次"的回调。（运行时形态见 `render-runtime.md §4`。）

`render` 是一个 **customization point object（CPO）**：`render(t, renderer)` **先找成员 `t.render(renderer)`，没有再走 ADL 自由函数 `render(t, renderer)`**——于是 `render` 既可由 `t` 自身实现，也可由自由函数为某个上下文类型实现。核心只要求 CPO 的调用结果是 sender：

```cpp
// 两种实现都可以：成员优先，ADL 兜底
struct t          { /* render_task */ render(renderer); };  // 成员，OK
/* render_task */   render(some_context, renderer);          // ADL 自由函数，OK

template <class T, class Renderer>
concept renderable = requires (T&& t, Renderer&& renderer)
{
    // 怎么画、要不要批量、要不要 SoA —— 全是它自己的事
    { render(::std::forward<T>(t), ::std::forward<Renderer>(renderer)) } -> ::stdexec::sender;
};
```

> 因为 CPO 对象与 ADL 自由函数同名 `render`，CPO 内部需隔离自己、避免解析回自身（`std::ranges::begin` 那套 poison-pill / 独立命名空间手法）。

引擎 / 库只保证一件事：把 renderer 交给 renderable，让每个 renderable **自己 `render(renderer)`**——启动一次，之后它自己每帧画自己。
不预设 sprite / mesh / 批处理 / SoA / 场景图。精神同 MUGEN：引擎是平台，内容是自包含的自治体（呼应 engine-spec §1）。

## 2. 三层分离（划清"界限"）

| 层 | 是什么 | 谁的事 | 现状 |
|---|---|---|---|
| **渲染器 / `renderer`** | Vulkan / OpenGL 生态位 | 最难、独立件 | 先用vulkan、不够再加 |
| **`renderable` 核心** | `render(renderer)` 概念 | 本架构的本体 | **已定**、极简、header-only |
| **动画** | 通过`renderable`对象的上下文决定要绘制什么内容 | **消费者侧可选辅助件** | **暂缓**，等 `renderer` 清楚 |

> "动画结构"：
>
> 游戏里并没有**动画**的概念，游戏主体负责的内容为：提供渲染环境，调动各个`renderable`进行`render`，即完成了一帧的绘制。游戏允许`renderable`对象有自己的**状态机**，这样`renderable`可以靠状态机来实现**动画**的效果。详情见后续章节*TODO*讨论

## 3. 核心契约（骨瘦如柴）

- 核心 = **`renderable` 概念**。不提供单帧容器，不定义跨 renderable 的顺序重排，也不定义单帧合成结构。
- **其余一概不规定**：`render` 怎么实现、renderer 怎么分派、批处理、SoA、单帧合成结构——**全自由**，各自 / 后端负责。
- renderer 对核心**不透明**：核心对 renderer 类型**泛型**，不固定其接口。
- **零开销可扩展**：renderable 边界保持模板 / sender 契约，内部全单态——呼应 `architecture.md T4a`「粗边界虚、热循环裸」。header-only 泛型核心。
- **批处理是消费者的旋钮**：一群小兵当**一个** renderable、内部一次批量；核心不强加。

## 4. 关键决策（讨论沉淀）

- **单一接入面**：只 `t.render(renderer)`。砍掉"注册可绘类型"那条第二层——怎么画它自己想。
- **不规定实现**：`render` 怎么实现与核心无关；它想怎么实现怎么实现。架构 = 契约（concept），**不是**实现。
- **单帧 ⟂ 时间轴**（关键洞见）：画单帧**根本不碰时间轴**；时间轴 = "给定 t → 解析出单帧参数"。两者是干净的缝。
- **权威动画态进 simulator/ 快照**：帧数据格斗 + 联网 →「当前帧 / clip / 变换」按 tick 在 sim 推进、写 ECS / 快照（engine-spec §4.4）；渲染只读快照 + 插值（位置插值、帧离散）。推进做成纯函数 `(state, dt_tick) → state`，sim 与表现同源。
- **小白与高手双赢**：小白 / 编辑器可以生成直接的 `render` 实现；高手全手写 `render`。核心不因此增加辅助函数层。

## 5. 英雄如何"无缝接入"

- **simulator tick**：英雄协程被 resume → `advance(dt_tick)` 推时间轴 → 写 当前 clip / 帧 / 变换 进 ECS / 快照。
- **渲染**：英雄在自己的 `main` 里把渲染协程 `render(renderer)` **注册一次**（spawn 到 render scheduler）；此后它自己每帧读快照 + 插值 alpha，把自己录进当帧画面。没有"外部每帧调用一次 `render`"的调用点——注册后由它自己的循环体逐帧录制。
- 变换矩阵、帧锚点、需要怎样的 pipeline / buffer / texture——**英雄自己解决**。

## 6. 渲染器 / `renderer`

> 机制与帧生命周期见 **`renderer.md`**（§3 五阶段、§4 并发模型、§5 scheduler 契约）；render task 在运行时怎么被启动 / 编排 / 收尾见 **`render-runtime.md`**；Vulkan 概念背景（in-flight / 帧槽 / dynamic rendering / 模型 B 示例）见 **`vulkan-qa.md`**。本节只记与 `renderable` 直接相关的约定。

- **`render(renderer)` 里的 `renderer`**：现阶段恒为唯一的 renderer 实现，它既是渲染器、又充当绘制上下文；renderable 在其上只录 draw，**不调 `begin / end_rendering`、不碰 submit / present**——帧结构归 renderer。`t.render(renderer)` 中携带上下文的是 `t`，`renderer` 这个位置不放别的东西。`render` 是一个注册一次、贯穿整个参与周期的协程（形态见 `render-runtime.md §4`）。
- **录进哪个 command buffer 取决于并发模型**：当前实现（模型 A）下 renderable 录进 renderer 的 **primary** command buffer，串行录制；选定的演进方向（模型 B）下改录进**自己的 secondary CB** 并交回，由 renderer 用 `vkCmdExecuteCommands` 按数组顺序决定绘制先后。两种模型下 renderable 都只认交给它的 command buffer（现阶段不排序、按提交顺序；排序策略待定）。
- **「核心不规定顺序」不变**：§3 说的是**核心 concept 不强加顺序**；顺序是 **renderer / 后端**的职责（§3「各自 / 后端负责」）。于是「自己画自己」（§1）与「跨 renderable 顺序」以**排序**调和，**不**引入单帧合成结构、也**不**引入 draw 抽象。

## 7. 动画（单帧结构 + 时间轴）——暂缓的可选辅助件

- **现在彻底不做**：它架在还没定型的 `draw` / `ctx` 上，提前做必引入意外复杂。
- 将来做也**极简**：只为可序列化、不强求通用、用得极少时才碰。
- 形态：可序列化**单帧合成结构**（什么图叠在什么图上、哪些图共享同一变换矩阵）+ **时间轴 / 影片**（携带时间）+（更后）逐帧 hit/hurt 框——即 engine-spec §4.8 预留的"可选辅助库"，给 Fighter-Factory 式编辑器往返保存用。手写英雄绕开它（代码即结构）。

## 8. 与 engine-spec 的关系

- **细化 / 明确**：engine-spec §4.5「英雄 `spawn` 渲染任务 → render sender 图」在此明确为「`renderable` + `render(renderer)`」这套显示架构；§4.8 的"可选辅助库"即本文第 7 节的动画辅助件。
- **一致**：simulator解耦、双缓冲快照 + 插值、软确定性；现阶段 renderer 只有 vulkan 实现。

## 9. 待定 / 未来

- 动画辅助件的极简形态与编辑器往返格式。
- **render context 可 dump（未来向）**：`render` 依赖的 context 应可从"一个 dump 出来的对象"重建、也可随时被 dump（服务快照 / 联网 / 离线恢复，呼应 engine-spec §4.4）。现阶段为控复杂度**不做**，但方向朝此走——瞬态 GPU 资源作为协程帧局部量持有，恰好使 dump/恢复只需重建耐久 context。
