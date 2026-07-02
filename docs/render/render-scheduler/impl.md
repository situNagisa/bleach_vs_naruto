# bvn 渲染·render scheduler 当前实现与妥协（现状 + 计划）

> 本文只讲**现状与计划**：当前用转发调度器带来的妥协、内部结构、以及固化 / 并行 / 排序的待办。
> 终态设计与契约见 ../render-scheduler.md；并发模型 A/B 见 ./model-ab.md。

---

## 1. 现状：render scheduler 是一个「转发调度器」

现阶段 render scheduler 做成一个**转发调度器**：它自己不直接拥有执行资源，而是转发给一个复用来的真实调度器。内部持有三样东西：

- 一个**任务队列**：装本帧被挂起、等着录制的 render task；
- 一个**内部调度器**：复用执行库现成的调度器，真正的录制在它上面跑（只要它满足模型 A 的串行约束即可）；
- 一个 **scope**：追踪上一帧编排出去的工作，用来在开下一帧前确认上一帧已收干净。

> **为什么转发**：这样能直接复用执行库现成的调度器（执行资源不用手写），也为将来的并行录制（模型 B）留好口子——把内部调度器换成允许并行的即可。代价见 §2。

**A 已走通**：模型 A（单 primary command buffer、串行录制）是 M0 / M1 的当前实现，作为当前契约与模型 B 的基线保留。

---

## 2. 妥协：调度器来源现阶段从 context 取，而非 env

理想情况下，render task 应像取 stop token 一样，从自身 env 取所在调度器（`get_scheduler(env)`）来 `co_await schedule`，统一走 env（见 ../render-task.md §2 结束信号那套）。但 render scheduler 是**转发调度器**：它把录制转发到内部调度器上跑，于是 task 醒来时 env 里的调度器会是那个**内部调度器**，而不是 render scheduler 本身——若再用 `get_scheduler(env)` 去调度，就绕过了任务队列、丢失了 render scheduler 对这些 task 的掌控（它们不再被 `submit()` 的帧开闭括住）。

- **现阶段的妥协**：render task 改从自身 context（`t` / context 携带的 render scheduler）取来 `co_await schedule`，绕开 `get_scheduler(env)`。这是**临时**做法，为的是不把转发调度器的复杂度一次性摊开。
- stop token 不受影响，全程从 env 取。

---

## 3. 计划

- **render scheduler 固化**：一旦确定内部到底用什么调度器，就把它**内联**进 render scheduler（render scheduler 固化、不再转发），届时 `get_scheduler(env)` 重新等于 render scheduler，调度回归统一、`co_await schedule` 也回到从 env 取（消除 §2 妥协）。stop token 全程不受影响。
- **并行录制（模型 B）落地**：把内部调度器换成允许并行的，`submit()` 的 `when_all` 天然并行录各自的 secondary command buffer，再由 `submit()` 按队列顺序 join（见 ./model-ab.md）。

---

## 4. 待定（动手时敲）

- **排序键（暂不做）**：游戏现阶段无需处理绘制排序，先按提交顺序。将来需要时再引入排序键，并定其取值与传输（task 返回值带出 vs 调度前由快照统一算好分配，倾向后者）。
- **task↔secondary 池的归属**：池由 renderable 自持，还是 scheduler 池化复用（避免每英雄一池的碎片）？
