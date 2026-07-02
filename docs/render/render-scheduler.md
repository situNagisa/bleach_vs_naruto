# bvn 渲染·render scheduler（设计 + 规范）

> render scheduler 的设计、职责、`submit()` 一帧编排、契约、收尾。这是**终态设计**视角。
> 当前转发调度器造成的妥协实现、内部结构、固化计划见 render-scheduler/impl.md；并发模型 A/B 见 render-scheduler/model-ab.md。
> render task 侧形态见 render-task.md；启动 / 注册见 boot.md。

---

## 1. 两条 scheduler

游戏主体提供两条独立的 scheduler：

| scheduler            | 跑什么                                    | 对外保证                                          |
| -------------------- | ----------------------------------------- | ------------------------------------------------- |
| **main scheduler**   | 每个 `entity.main`、entity 派生的逻辑任务 | ——                                                |
| **render scheduler** | 所有 render task                          | **串行**（模型 A：录进单条 primary command buffer） |

**为什么 render scheduler 要串行**：帧生命周期的铁律——同一个 command buffer 不能被并发录制，queue 的提交 / 呈现也不能并发（见 renderer.md §2）。让 render scheduler 对外保证串行录制，天然满足这条铁律（对应模型 A）。至于它内部用什么资源来兑现这个串行（几条线程、什么执行器），是调度器自己的事——**调度器对外只承诺串行 / 并行，线程是它内部分配的资源**。

> 现阶段 render scheduler 做成一个**转发调度器**（复用现成执行库的调度器），这带来一处妥协；详见 render-scheduler/impl.md。本文其余部分按终态设计描述。

它对外提供两件事：

- render task `co_await schedule(render_scheduler)` → 把自己挂进任务队列，等下一帧；
- 游戏主体 `render_scheduler.submit()` → 开一帧、放行队列里所有 task 去录制、收一帧（§2）。

---

## 2. `submit()` 与一帧的编排

### 2.1 `submit()` 做什么

帧的**开闭**——结束上一帧、为下一帧铺好台子、放行本帧所有 render task 录制——全在 render scheduler 的 `submit()` 里。游戏主体每帧调一次 `submit()`，一帧就走完。形态（伪代码，中性名）：

```cpp
void render_scheduler::submit()
{
    sync_wait(scope.on_empty());        // 1) 等上一帧编排出去的工作收干净

    // 2) 开帧 + 开 rendering：命令序列见 renderer-vulkan-impl.md §3，以 // + {} 内联（非具名函数），操作 renderer 持有的数据
    {
        // vkWaitForFences / vkAcquireNextImageKHR / … / vkCmdBeginRendering(renderer.primary_cmd, …)
    }

    auto work =
        when_all(task_queue)            // 3) 放行本帧所有已挂起的 render task 去录制
        | then([&]
        {
            // 4) 收 rendering + 收帧：命令序列见 renderer-vulkan-impl.md §5，同样 // + {} 内联
            {
                // vkCmdEndRendering(renderer.primary_cmd) / vkQueueSubmit2 / vkQueuePresentKHR / …
            }
        });
    scope.spawn(inner_scheduler, work); // 在内部调度器上编排；scope 追踪它
}
```

开帧 / 收帧的命令序列见 renderer-vulkan-impl.md §3 / §5——它们由 `submit()` 以 `//` 注释 + `{}` 块**内联**执行、操作 renderer 持有的 vulkan 数据，**不做具名函数**（每处只用一次）；renderer 本身是纯数据 context，不定义帧生命周期函数。**`vkCmdBeginRendering` / `EndRendering` 与 submit / present 永远由 `submit()` 执行**，render task 绝不自调。

于是**帧开闭在 `submit()`、render 开闭在 render task 循环体**——两者是不同层：`submit()` 负责「这一帧」（开台 / 收台 / 提交 / 呈现），render task 只负责「把自己录进这一帧」（bind / draw）。模型 A 下内部调度器保证串行，`when_all` 里各 task 就按队列顺序串行录进同一条 primary command buffer；模型 B 落地后内部调度器允许并行，各 task 并行录各自的 secondary，`submit()` 再按队列顺序 join（见 render-scheduler/model-ab.md）。

### 2.2 注册顺序即绘制顺序

render task 第一次 `co_await schedule(render_scheduler)` 挂进任务队列的先后，就是此后每一帧 `submit()` 放行它们录制的先后，也就是**绘制顺序**（FIFO）。现阶段不排序，按注册 / 提交顺序（排序键待定，见 render-scheduler/impl.md）。

### 2.3 启动与每帧

因为注册顺序即绘制顺序，游戏主体启动时按这个次序铺（详见 boot.md）：

1. 启动各 entity 的 `main`——每个 `main` 在 render scheduler 上**按绘制顺序**注册自己的 render task；
2. 让 render scheduler 空转一轮，确认这些 render task 都已停靠在各自第一个 `co_await schedule`（初始化都跑完、都挂起等第一帧）；
3. 此后游戏主体每帧调一次 `render_scheduler.submit()`，整帧（开台 → 各 task 录制 → 收台 + 提交 + 呈现）就走完。

主循环因此简化为：`while (!stop) render_scheduler.submit();`。

---

## 3. render scheduler 契约（当前模型 A）

供 scheduler 实现遵循：

1. **每帧 FIFO 流水**：`begin_frame → begin_rendering(PRIMARY) →（按注册顺序串行驱动所有 task 录进 primary）→ end_rendering → submit → present`。submit 前必须让本帧全部 content task 跑完。
2. **顺序归属**：当前模型 A 下绘制先后 = task 被 FIFO resume 后录进 primary command buffer 的顺序。**现阶段不排序**（按注册 / 提交顺序）。
3. **begin / end_rendering 归 `submit()`**；task 只认 `cmd`（当前 primary，未来 B 为 secondary），只录 draw，不碰 submit / present。
4. **frames-in-flight**：每帧槽独立 primary CB + sync；CPU 最多领先 GPU N 帧；CPU 每帧写的 GPU buffer 备 N 份（push constant 录进 CB 内，不在此列）。未来模型 B 下，每 task 的 secondary 池亦须按帧槽拆分。
5. **生命周期**：渲染收尾须先排空 render scope、再析构 renderable、最后拆 renderer（见 §4）。未来模型 B 下还包括 renderable 自持的 secondary 池。
6. **A↔B 与未来 CUDA**：task 只认 `cmd`，故 A↔B 切换、乃至换 CUDA renderer，renderable 无须改动。

**已定的演进方向**：单 entity 可注册**多个 render task**（未来模型 B 下对应多个 secondary；粒度落在 render task 而非 entity）——这样跨深度的情形（如 A 的剑要盖在更近的 B 前）也能表达。

---

## 4. 收尾次序

收尾次序（硬约束，详见 renderer.md §2.5 与 renderer-vulkan-impl.md §6）：

1. 发出结束信号 → 再让 render scheduler 跑一轮（`submit()` 或空转），使挂起的 task 醒来、观察到 stop、退出循环并清理（帧内局部量随协程帧析构释放）；
2. 排空 render scope（确保无 task 在录 / 在飞）、再排空 main scope；
3. `vkDeviceWaitIdle`；
4. renderer 拆除。

**第 1→2→4 步的次序不可调换**：render task 持有的 vulkan 帧内局部量，必须在 device 销毁前先析构干净，否则变野指针。（结束信号从 env 取，见 render-task.md §2。）

---

## 5. 一帧数据流（串起全文）

1. 游戏主体的主循环采输入、推进 sim、算好这一帧要画的数据（位置、当前帧、相机等）。
2. 主循环调用 `render_scheduler.submit()`：
	- 等上一帧收干净 → 开帧 + 开 rendering 开台（`submit()` 内联块）；
	- 放行本帧所有 render task，各自把自己录进 primary command buffer（按注册顺序串行）；
	- 收 rendering + 收帧收台：submit + present。
3. 结束信号发出时，按 §4 收尾。

> 单帧 ⟂ 时间轴：render task 只管「画这一帧的样子」，怎么从 tick 推进出「这一帧的样子」是 entity / sim 的事（见 ../animation.md）。
