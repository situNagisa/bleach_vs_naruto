# bvn 显示架构（Display Architecture）· 设计方向

> 日期：2026-06-24　状态：**方向已定**
> 本文记录「显示架构」的设计思想，**用于描述项目、非实现计划**。它把 `engine-spec.md §4.5`（英雄产出渲染任务）的思路，明确成一套独立的显示架构。

---

## 1. 核心哲学：一切可视物自己画自己

**引擎不建模"场景"。** 任何想出现在屏幕上的东西，都是一个 **`renderable`**——它知道在给定一个绘制上下文时**怎么画自己**：

```cpp
inline constexpr /* 未指定 */ render = /* 未指定 */; // 自定义点对象，支持 t.render(renderer) 调用

template <class T, class Renderer>
concept renderable = requires (T&& t, Renderer&& renderer)
{
    { render(::std::forward<decltype(t)>(t), ::std::forward<decltype(renderer)>(renderer)); } -> ::stdexec::sender; // 怎么画、要不要批量、要不要 SoA —— 全是它自己的事
};
```

>   其中，`t`是一个依赖于`renderer`类型的`renderable`

引擎 / 库**只干三件事**：**收集** renderable → **排序** → 让每个renderable**自己 `render(renderer)`**。
不预设 sprite / mesh / 批处理 / SoA / 场景图。精神同 MUGEN：引擎是平台，内容是自包含的自治体（呼应 engine-spec §1）。

## 2. 三层分离（划清"界限"）

| 层 | 是什么 | 谁的事 | 现状 |
|---|---|---|---|
| **渲染器 / `renderer`** | Vulkan / OpenGL 生态位 | 最难、独立件 | 先用vulkan、不够再加 |
| **`renderable` 核心** | `render(renderer)` 概念 + 收集 / 排序 / 组帧 | 本架构的本体 | **已定**、极简、header-only |
| **动画** | 通过`renderable`对象的上下文决定要绘制什么内容 | **消费者侧可选辅助件** | **暂缓**，等 `renderer` 清楚 |

> "动画结构"：
>
> 游戏里并没有**动画**的概念，游戏主体负责的内容为：提供渲染环境，调动各个`renderable`进行`render`，即完成了一帧的绘制。游戏允许`renderable`对象有自己的**状态机**，这样`renderable`可以靠状态机来实现**动画**的效果。详情见后续章节*TODO*讨论

## 3. 核心契约（骨瘦如柴）

- 核心 = **`renderable` 概念**+ **组合机制**（收集异质 renderable、按序逐个 `draw`）+ **可选便捷件**。
- **其余一概不规定**：`draw` 怎么实现、`ctx` 怎么 dispatch（虚 / 模板 / 命令流）、批处理、SoA、单帧合成结构——**全自由**，各自 / 后端负责。
- `ctx` 对核心**不透明**：核心对 `ctx` 类型**泛型**，不固定其接口。
- **零开销可扩展**：在 renderable 边界做**粗粒度类型擦除**（每 renderable / 每帧一次间接调用），内部全单态——呼应 `architecture.md T4a`「粗边界虚、热循环裸」。header-only 泛型核心。
- **批处理是消费者的旋钮**：一群小兵当**一个** renderable、内部一次批量；核心不强加。

## 4. 关键决策（讨论沉淀）

- **单一接入面**：只 `t.render(renderer, context)`。砍掉"注册可绘类型"那条第二层——怎么画它自己想。
- **不规定实现**：`render` 怎么实现与核心无关；它想怎么实现怎么实现。架构 = 契约（concept）+ 组合 + 便捷件，**不是**实现。
- **单帧 ⟂ 时间轴**（关键洞见）：画单帧**根本不碰时间轴**；时间轴 = "给定 t → 解析出单帧参数"。两者是干净的缝。
- **权威动画态进 simulator/ 快照**：帧数据格斗 + 联网 →「当前帧 / clip / 变换」按 tick 在 sim 推进、写 ECS / 快照（engine-spec §4.4）；渲染只读快照 + 插值（位置插值、帧离散）。推进做成纯函数 `(state, dt_tick) → state`，sim 与表现同源。
- **小白与高手双赢**：引擎给便利函数（"读 dt、推进、解析单帧、画"一行搞定）方便小白 / 编辑器；高手全手写 `render`。

## 5. 英雄如何"无缝接入"

- **simulator tick**：英雄协程被 resume → `advance(dt_tick)` 推时间轴 → 写 当前 clip / 帧 / 变换 进 ECS / 快照。
- **渲染**：英雄的渲染任务读快照 + 插值 alpha → `render(ctx)` 把自己画到画布；引擎收集所有 renderable、排序、深度缓冲解遮挡、组帧。
- 变换矩阵、帧锚点等——**英雄自己解决**，引擎只管彼此的渲染先后。

## 6. 渲染器 / `renderer`

- 

## 7. 动画（单帧结构 + 时间轴）——暂缓的可选辅助件

- **现在彻底不做**：它架在还没定型的 `draw` / `ctx` 上，提前做必引入意外复杂。
- 将来做也**极简**：只为可序列化、不强求通用、用得极少时才碰。
- 形态：可序列化**单帧合成结构**（什么图叠在什么图上、哪些图共享同一变换矩阵）+ **时间轴 / 影片**（携带时间）+（更后）逐帧 hit/hurt 框——即 engine-spec §4.8 预留的"可选辅助库"，给 Fighter-Factory 式编辑器往返保存用。手写英雄绕开它（代码即结构）。

## 8. 与 engine-spec 的关系

- **细化 / 明确**：engine-spec §4.5「英雄 `spawn` 渲染任务 → render sender 图」在此明确为「`renderable` + `render(ctx)` + 组合」这套显示架构；§4.8 的"可选辅助库"即本文第 7 节的动画辅助件。
- **一致**：simulator解耦、双缓冲快照 + 插值、软确定性、Vulkan→CUDA 后端可换（落在 `ctx` / `renderer`层）。

## 9. 待定 / 未来

- `ctx` / rhi 的正经接口（用例 + CUDA 驱动）。
- 动画辅助件的极简形态与编辑器往返格式。
- 组合机制的排序策略（先提交序，不够再加）。
