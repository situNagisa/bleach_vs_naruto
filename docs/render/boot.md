# bvn 渲染·启动（boot）

> 渲染侧「怎么被跑起来」：游戏主体只提供运行环境，entity 在自己的 `main` 里注册 render task；游戏主体不认识 entity 内部。
> 关联：render task 形态见 [render-task.md §1 render task 的形态](render-task.md#1-render-task-的形态)；render scheduler 与 `submit()` 见 [render-scheduler.md §2 submit() 与一帧的编排](render-scheduler.md#2-submit-与一帧的编排)；client 现状（entity 都放哪）见 [../client/impl.md §1](../client/impl.md#1-entity-都放在-client-下)。

---

## 1. 核心哲学：游戏主体只提供运行环境

游戏主体（`client` 可执行）**不认识**英雄、UI、输入是什么，它只做两件事：

1. 把每个 **entity** 跑起来——在 main scheduler 上启动 `entity.main`；
2. 给 entity 提供**设施**——两条 scheduler（main / render）、renderer、程序结束信号——并**每帧驱动 render scheduler 出一帧**。

entity 想做什么、想画什么，全在它自己的 `main` 里完成。游戏主体永远不替 entity 写「该怎么画英雄」「该怎么画 UI」这类逻辑。

> **entity 是什么**：任何东西。英雄、游戏 UI、输入处理、调试覆盖层、场地——都是 entity。它们彼此独立、互不知道对方存在。（当前 client 里有哪些 entity、它们代表什么，见 [../client/impl.md §1](../client/impl.md#1-entity-都放在-client-下)。）

---

## 2. 职责二分：游戏主体 vs entity 实现者

### 2.1 游戏主体的责任（只此而已）

- **持有设施**：窗口、renderer、裸 `::entt::registry` 世界、两条 scheduler、程序结束信号。
- **启动 entity**：对每个 entity，在 main scheduler 上 spawn 它的 `entity.main`。
- **每帧驱动渲染**：每帧调用 render scheduler 的 `submit()`（见 [render-scheduler.md §2](render-scheduler.md#2-submit-与一帧的编排)）。这是游戏主体在渲染侧唯一主动做的事——它**不**往 render scheduler 上放任何自己的 task：帧的开闭已经在 `submit()` 内部，不再需要一个单独的「帧任务」。
- **收尾**：当结束信号发出时，依次排空两条 scheduler，做清理，退出程序。

游戏主体**只能见到 entity 的一个接口：`main`**。它不知道 entity 内部有没有英雄状态机、有几个渲染任务、画的是 sprite 还是 mesh。

### 2.2 entity 实现者的责任

- 在 `entity.main` 里做自己需要的一切：读输入、推自己的状态、`spawn` 自己的 render task。
- 自己管理自己的资源、自己的并发、自己的绘制顺序内部细节。

### 2.3 这条边界划掉了什么

游戏主体**不**为 entity 提供：

- 「渲染英雄 / UI 的辅助函数」——怎么画是 entity 自己的事；
- 跨 entity 的「可绘类型注册表」「draw 抽象」「场景图」——核心不建模场景（见 [renderable.md §3 核心契约](renderable.md#3-核心契约骨瘦如柴)）；
- entity 之间的中间层——它们相互独立，不需要被抽象到一起。

> 一句话：**entity 的实现代码不进游戏主体**。如果你发现自己在游戏主体里写「针对某种 entity」的代码，那是放错了地方。

---

## 3. entity 怎么注册 render task

在 `entity.main`（跑在 main scheduler 上）里，entity 把自己的 render task spawn 到 **render scheduler** 上。注册就这一步，是 entity 唯一的渲染侧动作；对外它仍然只暴露 `main`。注册的先后即绘制顺序（见 [render-scheduler.md §2.2](render-scheduler.md#22-注册顺序即绘制顺序)）。

**英雄如何无缝接入**（一个 entity 的具体例子）：

- **simulator tick**：英雄协程被 resume → 推自己的时间轴 → 写 当前 clip / 帧 / 变换 进 ECS / 快照（见 [../animation.md §2 权威动画态](../animation.md#2-权威动画态进-simulator-快照关键设计)）。
- **渲染**：英雄在自己的 `main` 里把渲染协程 `render(renderer)` **注册一次**（spawn 到 render scheduler）；此后它自己每帧读快照 + 插值 alpha，把自己录进当帧画面。没有"外部每帧调用一次 `render`"的调用点——注册后由它自己的循环体逐帧录制。
- 变换矩阵、帧锚点、需要怎样的 pipeline / buffer / texture——**英雄自己解决**。

---

## 4. 启动次序与每帧

因为注册顺序即绘制顺序，游戏主体启动时按这个次序铺：

1. 启动各 entity 的 `main`——每个 `main` 在 render scheduler 上**按绘制顺序**注册自己的 render task；
2. 让 render scheduler 空转一轮，确认这些 render task 都已停靠在各自第一个 `co_await schedule`（初始化都跑完、都挂起等第一帧）；
3. 此后游戏主体每帧调一次 `render_scheduler.submit()`，整帧走完。

主循环因此简化为：`while (!stop) render_scheduler.submit();`。收尾见 [render-scheduler.md §4](render-scheduler.md#4-收尾次序)。
