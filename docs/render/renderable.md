# bvn 渲染·renderable（设计 + 规范）

> renderable 的设计思想与实现规范：一切可视物自己画自己。
> 关联：render task（render 协程的运行时形态）见 [render-task.md §1](render-task.md#1-render-task-的形态)；renderer（画的上下文）见 [renderer.md §1](renderer.md#1-renderer-的形态与职责划分)；启动 / 注册见 [boot.md §3](boot.md#3-entity-怎么注册-render-task)；动画辅助件见 [../animation.md](../animation.md#bvn-动画系统animation)。

---

## 1. 核心哲学：一切可视物自己画自己

**引擎不建模"场景"。** 任何想出现在屏幕上的东西，都是一个 **`renderable`**——它知道**怎么把自己画进每一帧**。

`render` 是一个 **customization point object（CPO）**：`render(t, renderer)` **先找成员 `t.render(renderer)`，没有再走 ADL 自由函数 `render(t, renderer)`**——于是 `render` 既可由 `t` 自身实现，也可由自由函数为某个上下文类型实现。核心只要求 CPO 的调用结果是 sender：

```cpp
// 两种实现都可以：成员优先，ADL 兜底
struct t          { /* render_task */ render(renderer); };  // 成员，OK
/* render_task */   render(some_context, renderer);          // ADL 自由函数，OK

inline constexpr struct render_t {
	constexpr static? decltype(auto) operator()(auto&& t, auto&& renderer) const? noexcept(?)
		requires ...
	{
		if constexpr member call
			member call
		else if constexpr adl call
			adl call
		else
			fail
	}
} render{}; // CPO

template <class T, class Renderer>
concept renderable = requires (T&& t, Renderer&& renderer)
{
    // 怎么画、要不要批量、要不要 SoA —— 全是它自己的事
    { render(::std::forward<T>(t), ::std::forward<Renderer>(renderer)) } -> ::stdexec::sender;
};
```

> 因为 CPO 对象与 ADL 自由函数同名 `render`，CPO 内部需隔离自己、避免解析回自身（`std::ranges::begin` 那套 poison-pill / 独立命名空间手法）。

引擎 / 库只保证一件事：把 renderer 交给 renderable，让每个 renderable **自己 `render(renderer)`**——启动一次，之后它自己每帧画自己。不预设 sprite / mesh / 批处理 / SoA / 场景图。精神同 MUGEN：引擎是平台，内容是自包含的自治体。

> `render(renderer)` 返回的 sender / 协程**贯穿 `t` 参与渲染的整个生命周期**（初始化 → 逐帧录制 → 收尾），不是"每帧被外部调用一次"的回调。其运行时形态见 [render-task.md §1](render-task.md#1-render-task-的形态)。

---

## 2. 三层分离（划清界限）

| 层 | 是什么 | 谁的事 |
|---|---|---|
| **渲染器 / `renderer`** | Vulkan / OpenGL 生态位 | 最难、独立件，见 [renderer.md](renderer.md#bvn-渲染renderer设计-规范) |
| **`renderable` 核心** | `render(renderer)` 概念 | 本文·极简·header-only |
| **动画** | 通过 renderable 的上下文决定绘制什么内容 | 消费者侧可选辅助件，暂缓，见 [../animation.md](../animation.md#bvn-动画系统animation) |

---

## 3. 核心契约（骨瘦如柴）

- 核心 = **`renderable` 概念**。不提供单帧容器，不定义跨 renderable 的顺序重排，也不定义单帧合成结构。
- **其余一概不规定**：`render` 怎么实现、renderer 怎么分派、批处理、SoA、单帧合成结构——**全自由**，各自 / 后端负责。
- renderer 对核心**不透明**：核心对 renderer 类型**泛型**，不固定其接口。
- **零开销可扩展**：renderable 边界保持模板 / sender 契约，内部全单态（粗边界虚、热循环裸）。header-only 泛型核心。
- **批处理是消费者的旋钮**：一群小兵当**一个** renderable、内部一次批量；核心不强加。
- **单一接入面**：只 `render(t, renderer)` 这一个 CPO。不建"注册可绘类型"那条第二层——怎么画它自己想。
- **顺序不归核心**：核心 concept 不强加绘制顺序；跨 renderable 的顺序是 render scheduler / 后端的职责（见 [render-scheduler.md §2.2 注册顺序即绘制顺序](render-scheduler.md#22-注册顺序即绘制顺序)），以**排列**调和，不引入单帧合成结构、也不引入 draw 抽象。

---

## 4. 关键决策（就近）

- **单帧 ⟂ 时间轴**（关键洞见）：画单帧**根本不碰时间轴**；时间轴 = "给定 t → 解析出单帧参数"。两者是干净的缝。权威动画态如何按 tick 推进见 [../animation.md §2 权威动画态](../animation.md#2-权威动画态进-simulator-快照关键设计)。
- **不规定实现**：架构 = 契约（concept），**不是**实现；`render` 想怎么实现怎么实现。
- **小白与高手双赢**：小白 / 编辑器可以生成直接的 `render` 实现；高手全手写 `render`。核心不因此增加辅助函数层。
