# bvn 渲染运行时（Render Runtime）· 设计方向

> 日期：2026-07-01　状态：**方向已定**
> 本文描述「程序怎么把渲染跑起来」：`main` 如何启动、两条调度器（scheduler）怎么分工、谁负责什么、一帧怎么从开始走到结束。
> 它衔接 `display-architecture.md`（一切可视物自己 `render`）与 `renderer.md`（一帧的 Vulkan 生命周期）：前者讲「画什么」，后者讲「怎么画一帧」，本文讲「谁在什么时机驱动这些」。

---

## 1. 核心哲学：游戏主体只提供运行环境

游戏主体（`client` 可执行）**不认识**英雄、UI、输入是什么，它只做两件事：

1. 把每个 **entity** 跑起来——在 main scheduler 上启动 `entity.main`；
2. 给 entity 提供**设施**——两条 scheduler（main / render）、renderer、程序结束信号——并**每帧驱动 render scheduler 出一帧**（§5）。

entity 想做什么、想画什么，全在它自己的 `main` 里完成。游戏主体永远不替 entity 写「该怎么画英雄」「该怎么画 UI」这类逻辑。这条边界是本文的主轴，§2 展开。

> **entity 是什么**：任何东西。英雄、游戏 UI、输入处理、调试覆盖层、场地——都是 entity。它们彼此独立、互不知道对方存在。`client` 现在的 `arena` / `hero` / `debug_overlay` 就是三个 entity，分别代表三个互不相干的开发者各自的产出。

---

## 2. 职责二分：游戏主体 vs entity 实现者

把世界分成清清楚楚的两侧，是不再写出冗余代码的关键。

### 2.1 游戏主体的责任（只此而已）

- **持有设施**：窗口、renderer、裸 `::entt::registry` 世界、两条 scheduler（见 §3）、程序结束信号。
- **启动 entity**：对每个 entity，在 main scheduler 上 spawn 它的 `entity.main`。
- **每帧驱动渲染**：每帧调用 render scheduler 的 `submit()`，让它开一帧、放行本帧所有 render task 录制、收一帧（见 §5）。这是游戏主体在渲染侧唯一主动做的事——它**不**往 render scheduler 上放任何自己的 task：帧的开闭已经在 `submit()` 内部，不再需要一个单独的「帧任务」。
- **收尾**：当结束信号发出时，依次排空两条 scheduler，做清理，退出程序。

游戏主体**只能见到 entity 的一个接口：`main`**。它不知道 entity 内部有没有英雄状态机、有几个渲染任务、画的是 sprite 还是 mesh。

### 2.2 entity 实现者的责任

- 在 `entity.main` 里做自己需要的一切：读输入、推自己的状态、`spawn` 自己的 render task。
- 自己管理自己的资源、自己的并发、自己的绘制顺序内部细节。

### 2.3 这条边界划掉了什么

游戏主体**不**为 entity 提供：

- 「渲染英雄 / UI 的辅助函数」——怎么画是 entity 自己的事；
- 跨 entity 的「可绘类型注册表」「draw 抽象」「场景图」——`display-architecture.md §3` 已说核心不建模场景；
- entity 之间的中间层——它们相互独立，不需要被抽象到一起。

> 一句话：**entity 的实现代码不进游戏主体**。如果你发现自己在游戏主体里写「针对某种 entity」的代码，那是放错了地方。

---

## 3. 两条 scheduler

游戏主体提供两条独立的 scheduler：

| scheduler            | 跑什么                                    | 对外保证                                          |
| -------------------- | ----------------------------------------- | ------------------------------------------------- |
| **main scheduler**   | 每个 `entity.main`、entity 派生的逻辑任务 | ——                                                |
| **render scheduler** | 所有 render task（§4）                     | **串行**（模型 A：录进单条 primary command buffer） |

**为什么 render scheduler 要串行**：`renderer.md §3` 的铁律——同一个 command buffer 不能被并发录制，`VkQueue` 的提交 / 呈现也不能并发。让 render scheduler 对外保证串行录制，天然满足这条铁律（对应 `renderer.md` 模型 A：单 primary command buffer、串行录制）。至于它内部用什么资源来兑现这个串行（几条线程、什么执行器），是调度器自己的事，本文不谈——**调度器对外只承诺串行 / 并行，线程是它内部分配的资源**。

### 3.1 render scheduler 是一个「转发调度器」（现阶段实现）

现阶段 render scheduler 做成一个**转发调度器**：它自己不直接拥有执行资源，而是转发给一个复用来的真实调度器。它内部持有三样东西：

- 一个**任务队列**：装本帧被挂起、等着录制的 render task；
- 一个**内部调度器**：复用执行库现成的调度器，真正的录制在它上面跑（只要它满足模型 A 的串行约束即可）；
- 一个 **scope**：追踪上一帧编排出去的工作，用来在开下一帧前确认上一帧已收干净。

它对外提供两件事：

- render task `co_await schedule(render_scheduler)` → 把自己挂进任务队列，等下一帧；
- 游戏主体 `render_scheduler.submit()` → 开一帧、放行队列里所有 task 去录制、收一帧（见 §5）。

> **为什么转发**：这样能直接复用执行库现成的调度器（执行资源不用手写），也为将来的并行录制（`renderer.md` 模型 B）留好口子——把内部调度器换成允许并行的即可。代价是 `co_await schedule` 的来源现阶段要走 context 而非 env，见 §6。

---

## 4. render task 的形态

一个 **render task** 就是 `t.render(renderer)` **本身**——`display-architecture.md §1` 那个接入面返回的协程（sender），跑在 render scheduler 上，代表「`t` 参与每一帧的绘制」。它**注册一次**，自己在内部完成 初始化 → 逐帧录制循环 → 收尾；render task 就等于 `render` 这个协程。

> `render` **只有一个参数 `renderer`**。现阶段 `renderer` 就是那个唯一的 renderer 实现，本质是一个保存 vulkan 上下文（instance / device / 当帧 command buffer 等）的 context。`render` 体内的**正确用法 = 直接调 vulkan 函数，把 renderer 携带的这些 handle 传进去**。整个游戏环境（结束信号、scheduler 那些设施）不放进这个参数位——那些从协程自身的 env 取（§6）；`t` 才是携带上下文的一方。用 `t`（而非 entity 本身）当主语，是为了不强求 `render` 必须由 entity 实现：entity 可以让它管理的另一个对象去实现 `render`（§4.2）。

### 4.1 常见形态：持续参与每一帧

render task 最常见的形态（伪代码；`render` 即 `t` 的 `render` 成员，函数本身就是协程）：

```cpp
auto T::render(renderer& r) -> render_task
{
    auto env  = co_await environment();
    auto stop = get_stop_token(env);      // 结束信号：恒从自身 env 取（§6）
    auto sched = /* t / context 携带的 render scheduler（现阶段临时从 context 取，§6） */;

    // 初始化：GPU 资源作为协程帧内的局部量，用 RAII 拥有者持有（构造即建、析构即毁）。
    // 就是一串 vkCreate*，首参是 r 携带的 device；活过整个循环。
    auto pipeline = make_pipeline(r);
    auto vertices = make_buffer(r);
    // 只该建一次，不写 if (== VK_NULL_HANDLE) 守卫；要表达不变式用 assert。

    while (!stop.stop_requested())
    {
        co_await schedule(sched);         // 挂起，等下一帧 submit() 放行自己
        // 醒来时一定在「帧已开启」窗口内（§5 保证）：取 r 携带的当帧 command buffer，
        // 直接调 vkCmd* 把自己的 draw 录进去。
        auto cmd = /* r 携带的当帧 command buffer */;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, /* 自己的 pipeline */);
        vkCmdBindVertexBuffers(cmd, /* … */);
        vkCmdDraw(cmd, /* … */);
    }
    // 退出循环：pipeline / vertices 随协程帧析构自动释放，无手动 teardown、无逐点判空。
}
```

要点：

- **render task 就是 `render` 协程**：注册一次，自己跑完 init → 逐帧录制 → 收尾；体内直接调 vulkan，参数只有 `renderer`。
- **初始化在循环前**：用 `renderer` 携带的 vulkan 上下文（device 等）建好自建资源，作为**协程帧内的局部量**持有（瞬态态 → 协程局部，见「状态二分」），生命周期用 RAII 拥有者管理。因为只建一次，不写 `if (handle == VK_NULL_HANDLE)` 初始化守卫；要表达「此处应为空」这类不变式用 `assert`。
- **循环体每帧录一次**：`co_await schedule(sched)` 挂起到下一帧；醒来后调 `vkCmd*` 把 draw 录进 renderer 交给的当帧 command buffer。
- **清理在循环后**：观察到结束信号、退出循环，帧内局部量随协程帧析构自动释放——无需手动 teardown，也不在调用点逐个判空。

### 4.2 高度自定义留有的口子

这个形态只是**预设的常见写法**，不是约束。设计刻意留了这些口子：

- **一次性绘制**：可以不写循环，借别人的 pipeline 画一次就销毁——pipeline 仍归别人持有。
- **多个 render task**：一个 entity 可以在不同时机注册很多个 render task。绘制粒度落在 **render task** 上，不是 entity——这样「A 的剑要盖在更近的 B 前面」这种跨深度情形也能用多个 task 表达。
- **谁来实现 `render`**：可以是 entity 本身，也可以是 entity 管理的另一个对象（呼应「用 `t` 当主语」）。

### 4.3 entity 怎么注册 render task

在 `entity.main`（跑在 main scheduler 上）里，entity 把自己的 render task spawn 到 **render scheduler** 上。注册就这一步，是 entity 唯一的渲染侧动作；对外它仍然只暴露 `main`。注册的先后即绘制顺序（§5.2）。

---

## 5. `submit()` 与一帧的编排

### 5.1 `submit()` 做什么

帧的**开闭**——结束上一帧、为下一帧铺好台子、放行本帧所有 render task 录制——全在 render scheduler 的 `submit()` 里。游戏主体每帧调一次 `submit()`，一帧就走完。形态（伪代码，中性名）：

```cpp
void render_scheduler::submit()
{
    sync_wait(scope.on_empty());        // 1) 等上一帧编排出去的工作收干净
    renderer.begin_frame();             // 2) 开帧：acquire + reset + begin CB
    renderer.begin_rendering();         //    开 dynamic rendering
    auto work =
        when_all(task_queue)            // 3) 放行本帧所有已挂起的 render task 去录制
        | then([&]
        {
            renderer.end_rendering();   // 4) 收帧：end rendering + submit + present
            renderer.end_frame();
        });
    scope.spawn(inner_scheduler, work); // 在内部调度器上编排；scope 追踪它
}
```

`begin_frame` / `end_frame` 内部对应 `renderer.md §3.2 / §3.4` 的 Vulkan 帧生命周期（acquire、布局转换、`vkCmdBeginRendering` / `EndRendering`、submit、present）。**`vkCmdBeginRendering` / `EndRendering` 与 submit / present 永远归 renderer**（`renderer.md §3.2` 契约），render task 绝不自调。

于是**帧开闭在 `submit()`、render 开闭在 render task 循环体**——两者是不同层：`submit()` 负责「这一帧」（开台 / 收台 / 提交 / 呈现），render task 只负责「把自己录进这一帧」（bind / draw）。模型 A 下内部调度器保证串行，`when_all` 里各 task 就按队列顺序串行录进同一条 primary command buffer；模型 B 落地后内部调度器允许并行，各 task 并行录各自的 secondary，`submit()` 再按队列顺序 join。

### 5.2 注册顺序即绘制顺序

render task 第一次 `co_await schedule(render_scheduler)` 挂进任务队列的先后，就是此后每一帧 `submit()` 放行它们录制的先后，也就是**绘制顺序**（FIFO）。现阶段不排序，按注册 / 提交顺序（排序键待定，见 `renderer.md §5`）。

### 5.3 启动与每帧

因为注册顺序即绘制顺序，游戏主体启动时按这个次序铺：

1. 启动各 entity 的 `main`——每个 `main` 在 render scheduler 上**按绘制顺序**注册自己的 render task；
2. 让 render scheduler 空转一轮，确认这些 render task 都已停靠在各自第一个 `co_await schedule`（初始化都跑完、都挂起等第一帧）；
3. 此后游戏主体每帧调一次 `render_scheduler.submit()`，整帧（开台 → 各 task 录制 → 收台 + 提交 + 呈现）就走完。

主循环因此简化为：`while (!stop) render_scheduler.submit();`。

---

## 6. 程序结束信号 & 调度器来源

### 6.1 结束信号：恒从 env 取

结束信号通过**协程自己的 stop token** 传递，从 env 取，**不通过 context**。

- 协程在自己的 stdexec 环境（env）里查询 stop token：`stdexec::get_stop_token(env)`，循环条件就是 `!stop.stop_requested()`。
- stop token 沿调度链从上游传到协程的 env——上游（scheduler / scope / 父任务）持有 stop source，请求停止时，协程下次醒来即看到 `stop_requested() == true`，退出循环、跑清理、结束。
- 这样 render task 的「该退出了」是它**自己从环境读出来的**，而不是去翻一个共享的全局标志。各 task 互不依赖同一个全局标志，结束信号的来源是统一的、标准的。

### 6.2 调度器来源：现阶段临时从 context 取，终态回 env

理想情况下，render task 应像取 stop token 一样，从自身 env 取所在调度器（`get_scheduler(env)`）来 `co_await schedule`，统一走 env。但 render scheduler 是**转发调度器**：它把录制转发到内部调度器上跑，于是 task 醒来时 env 里的调度器会是那个**内部调度器**，而不是 render scheduler 本身——若再用 `get_scheduler(env)` 去调度，就绕过了任务队列、丢失了 render scheduler 对这些 task 的掌控（它们不再被 `submit()` 的帧开闭括住）。

- **现阶段的妥协**：render task 改从自身 context（`t` / context 携带的 render scheduler）取来 `co_await schedule`，绕开 `get_scheduler(env)`。这是**临时**做法，为的是不把转发调度器的复杂度一次性摊开。
- **终态**：一旦确定内部到底用什么调度器，就把它**内联**进 render scheduler（render scheduler 固化、不再转发），届时 `get_scheduler(env)` 重新等于 render scheduler，调度回归统一、`co_await schedule` 也回到从 env 取。stop token 全程不受影响。

### 6.3 收尾次序

收尾次序（硬约束，详见 `renderer.md §3.5`）：

1. 发出结束信号 → 再让 render scheduler 跑一轮（`submit()` 或空转），使挂起的 task 醒来、观察到 stop、退出循环并清理（帧内局部量随协程帧析构释放）；
2. 排空 render scope（确保无 task 在录 / 在飞）、再排空 main scope；
3. `vkDeviceWaitIdle`；
4. renderer 拆除。

**第 1→2→4 步的次序不可调换**：render task 持有的 Vulkan 帧内局部量，必须在 device 销毁前先析构干净，否则变野指针。

---

## 7. 一帧数据流（串起全文）

1. 游戏主体的主循环采输入、推进 sim、算好这一帧要画的数据（位置、当前帧、相机等）。
2. 主循环调用 `render_scheduler.submit()`：
	- 等上一帧收干净 → `begin_frame` + `begin_rendering` 开台；
	- 放行本帧所有 render task，各自把自己录进 primary command buffer（按注册顺序串行）；
	- `end_rendering` + `end_frame` 收台：submit + present。
3. 结束信号发出时，按 §6 收尾。

> 单帧 ⟂ 时间轴：render task 只管「画这一帧的样子」，怎么从 tick 推进出「这一帧的样子」是 entity / sim 的事（`display-architecture.md §4`）。

---

## 8. 与其它文档的关系

- **`display-architecture.md`**：定义 `renderable` 与 `t.render(renderer)` 这个接入面（画什么、怎么画）。本文定义这些 render task 在运行时怎么被启动、编排、收尾。
- **`renderer.md`**：定义一帧的 Vulkan 生命周期（五阶段、并发模型、scheduler 契约）。本文 `submit()` 调用的 `begin_frame` / `end_frame` 即落在那套生命周期上。
- **`engine-spec.md §4.5`**：「英雄发起调度渲染任务」「后端可换点 renderer」在此具体化为：entity 在 `main` 里向 render scheduler 注册 render task；`render` 的参数位现阶段恒为 renderer 本身（唯一的 renderer 实现）。

---

## 9. 待定 / 未来

- **render scheduler 固化**：把内部调度器内联进 render scheduler、消除转发，使 `co_await schedule` 回到从 env 取（§6.2 终态）；届时确定内部到底用什么执行资源。
- **并行录制**（`renderer.md` 模型 B）落地后：把内部调度器换成允许并行的，`submit()` 的 `when_all` 天然并行录各自的 secondary command buffer，再由 renderer 按队列顺序 join。
- **render context 可 dump**：`render` 依赖的 context 应可被 dump、也可从 dump 出的对象重建（服务快照 / 联网 / 离线恢复，呼应 `engine-spec §4.4` 与 `display-architecture.md §9`）——现在为控复杂度不做，方向朝此走。
