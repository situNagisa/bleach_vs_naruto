# bvn 渲染·render scheduler（设计 + 规范）

> render scheduler 的设计、职责、`submit()` 一帧编排、契约、收尾。这是**终态设计**视角，按选定方向**模型 B**（并行录 secondary、submit 按序 join）描述。
> 当前基线是模型 A（串行 primary）——它是 B 的一个退化特例，连同转发调度器的妥协、内部结构、固化 / 并行计划见 [render-scheduler/impl.md](render-scheduler/impl.md#bvn-渲染render-scheduler-当前实现与妥协现状-计划)；两个模型的约束对照见 [render-scheduler/model-ab.md](render-scheduler/model-ab.md#bvn-渲染并发模型-a-b设计)。
> render task 侧形态见 [render-task.md](render-task.md#bvn-渲染render-task设计-规范)；启动 / 注册见 [boot.md](boot.md#bvn-渲染启动boot)。

---

## 1. 两条 scheduler

游戏主体提供两条独立的 scheduler：

| scheduler            | 跑什么                                    | 对外保证                                          |
| -------------------- | ----------------------------------------- | ------------------------------------------------- |
| **main scheduler**   | 每个 `entity.main`、entity 派生的逻辑任务 | ——                                                |
| **render scheduler** | 所有 render task                          | **绘制顺序 = 注册顺序**（各 task 可并行录制，`submit()` 按序 join） |

render scheduler 对外承诺的是**绘制先后**，不是"串行录制"——各 render task 可以并行录各自的 command buffer，`submit()` 再按注册顺序把它们 join 成一帧的绘制序列。至于它内部用什么资源来兑现这个并行 + 保序（几条线程、什么执行器），是调度器自己的事——**调度器对外只承诺顺序语义，线程是它内部分配的资源**。

> 为什么并行录制合法、以及"保序"具体怎么用排列而非抽象实现，见 [model-ab.md §3 模型 B](render-scheduler/model-ab.md#3-模型-bsecondary-command-buffer并行录制-控序-设计方向)。当前基线（模型 A）退化成"串行录进单条 primary"，是这套顺序语义的一个特例，见 [impl.md](render-scheduler/impl.md#bvn-渲染render-scheduler-当前实现与妥协现状-计划)。

它对外提供两件事：

- render task `co_await schedule(render_scheduler)` → 把自己挂进任务队列，等下一帧；
- 游戏主体 `render_scheduler.submit()` → 开一帧、放行队列里所有 task 去录制、按序 join、收一帧（[§2](#2-submit-与一帧的编排)）。

---

## 2. `submit()` 与一帧的编排

### 2.1 `submit()` 做什么

帧的**开闭**——结束上一帧、为下一帧铺好台子、放行本帧所有 render task 录制、按序 join——全在 render scheduler 的 `submit()` 里。游戏主体每帧调一次 `submit()`，一帧就走完。形态（伪代码，中性名）：

```cpp
void render_scheduler::submit()
{
    sync_wait(scope.on_empty());        // 1) 等上一帧编排出去的工作收干净

    // 2) 开帧 + 开 rendering：命令序列见 renderer-vulkan-impl.md §3，以 // + {} 内联（非具名函数），操作 renderer 持有的数据
    {
        // vkWaitForFences / vkAcquireNextImageKHR / … / vkCmdBeginRendering(…)
    }

    auto work =
        when_all(task_queue)            // 3) 放行本帧所有已挂起的 render task：各自并行录进自己的 secondary
        | then([&]
        {
            // 4) 按注册顺序 join（vkCmdExecuteCommands 数组序 = 绘制序），再收 rendering + 收帧
            //    命令序列见 renderer-vulkan-impl.md §5，同样 // + {} 内联
            {
                // vkCmdExecuteCommands(primary, secondaries[按注册序]) / vkCmdEndRendering / vkQueueSubmit2 / vkQueuePresentKHR / …
            }
        });
    scope.spawn(inner_scheduler, work); // 在内部调度器上编排；scope 追踪它
}
```

开帧 / 收帧的命令序列见 [renderer-vulkan-impl.md §3](renderer-vulkan-impl.md#3-每帧环境开帧-开-rendering在所有-task-之前) / [§5](renderer-vulkan-impl.md#5-结束一帧收-rendering-收帧)——它们由 `submit()` 以 `//` 注释 + `{}` 块**内联**执行、操作 renderer 持有的 vulkan 数据，**不做具名函数**（每处只用一次）；renderer 本身是纯数据 context，不定义帧生命周期函数。**开 / 收 rendering 与 submit / present 永远由 `submit()` 执行**，render task 绝不自调。

于是**帧开闭在 `submit()`、render 开闭在 render task 循环体**——两者是不同层：`submit()` 负责「这一帧」（开台 / 收台 / join / 提交 / 呈现），render task 只负责「把自己录进这一帧」（bind / draw）。`when_all` 里各 task 并行录各自的 secondary，`submit()` 再按队列顺序 `vkCmdExecuteCommands` join——**并行录制、串行保序**（见 [model-ab.md §3](render-scheduler/model-ab.md#3-模型-bsecondary-command-buffer并行录制-控序-设计方向)）。

> 当前基线（模型 A）下内部调度器只提供串行执行，`when_all` 退化成按队列顺序串行录进同一条 primary，join 步骤为空——语义等价、少一层 secondary（见 [impl.md](render-scheduler/impl.md#bvn-渲染render-scheduler-当前实现与妥协现状-计划)）。

### 2.2 注册顺序即绘制顺序

render task 第一次 `co_await schedule(render_scheduler)` 挂进任务队列的先后，就是此后每一帧 `submit()` join 它们的先后，也就是**绘制顺序**（FIFO）。`submit()` 按这个顺序排列各 task 的 secondary（`vkCmdExecuteCommands` 数组序 = 绘制序）。现阶段不按排序键重排，即注册序，排序键待定（见 [impl.md §4 待定](render-scheduler/impl.md#4-待定动手时敲)）。

### 2.3 启动与每帧

因为注册顺序即绘制顺序，游戏主体启动时按这个次序铺（详见 [boot.md](boot.md#bvn-渲染启动boot)）：

1. 启动各 entity 的 `main`——每个 `main` 在 render scheduler 上**按绘制顺序**注册自己的 render task；
2. 让 render scheduler 空转一轮，确认这些 render task 都已停靠在各自第一个 `co_await schedule`（初始化都跑完、都挂起等第一帧）；
3. 此后游戏主体每帧调一次 `render_scheduler.submit()`，整帧（开台 → 各 task 并行录制 → 按序 join → 收台 + 提交 + 呈现）就走完。

主循环因此简化为：`while (!stop) render_scheduler.submit();`。

---

## 3. render scheduler 契约

供 scheduler 实现遵循（按模型 B 描述；模型 A 是其退化特例）：

1. **每帧 FIFO 流水**：`begin_frame → begin_rendering →（放行所有 task 并行录制）→ 按注册序 join → end_rendering → submit → present`。join / submit 前必须让本帧全部 content task 录完。
2. **顺序归属**：绘制先后 = task 按注册顺序被 `submit()` join 的顺序（`vkCmdExecuteCommands` 数组序）。**现阶段不按排序键重排**（按注册 / 提交顺序）。
3. **开 / 收 rendering 归 `submit()`**；task 只认 renderer 从 frame-env 交给它的 `command_buffer()`（自己的 secondary），只录 draw，不碰 join / submit / present。
4. **frames-in-flight**：每帧槽独立同步对象；CPU 最多领先 GPU N 帧；CPU 每帧写的 GPU buffer 备 N 份（push constant 录进 CB 内，不在此列）。每 task 的 secondary 池亦须按帧槽拆分。
5. **生命周期**：渲染收尾须先排空 render scope、再析构 renderable（含其自持的 secondary 池）、最后拆 renderer（见 [§4](#4-收尾次序)）。
6. **模型无关 / 未来 CUDA**：task 只认 frame-env 的 `command_buffer()`，故 A↔B 切换、乃至换 CUDA renderer，renderable 无须改动（frame-env 二分见 [renderer.md §1.0](renderer.md#10-renderer-是一个-conceptvulkan-是它的一份实现)）。

**已定的设计粒度**：单 entity 可注册**多个 render task**（对应多个 secondary；粒度落在 render task 而非 entity）——这样跨深度的情形（如 A 的剑要盖在更近的 B 前）也能表达。

---

## 4. 收尾次序

收尾次序（硬约束，详见 [renderer.md §2.5](renderer.md#25-结束全部关机) 与 [renderer-vulkan-impl.md §6](renderer-vulkan-impl.md#6-结束全部关机)）：

1. 发出结束信号 → 再让 render scheduler 跑一轮（`submit()` 或空转），使挂起的 task 醒来、观察到 stop、退出循环并清理（帧内局部量随协程帧析构释放）；
2. 排空 render scope（确保无 task 在录 / 在飞）、再排空 main scope；
3. `vkDeviceWaitIdle`；
4. renderer 拆除。

**第 1→2→4 步的次序不可调换**：render task 持有的 vulkan 帧内局部量，必须在 device 销毁前先析构干净，否则变野指针。（结束信号从 env 取，见 [render-task.md §2](render-task.md#2-结束信号恒从-env-取)。）

---

## 5. 一帧数据流（串起全文）

1. 游戏主体的主循环采输入、推进 sim、算好这一帧要画的数据（位置、当前帧、相机等）。
2. 主循环调用 `render_scheduler.submit()`：
	- 等上一帧收干净 → 开帧 + 开 rendering 开台（`submit()` 内联块）；
	- 放行本帧所有 render task，各自并行把自己录进自己的 secondary command buffer；
	- 按注册序 join（`vkCmdExecuteCommands`）→ 收 rendering + 收帧：submit + present。
3. 结束信号发出时，按 [§4](#4-收尾次序) 收尾。

> 单帧 ⟂ 时间轴：render task 只管「画这一帧的样子」，怎么从 tick 推进出「这一帧的样子」是 entity / sim 的事（见 [../animation.md](../animation.md#bvn-动画系统animation)）。
