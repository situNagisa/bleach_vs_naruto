# bvn 渲染·render task（设计 + 规范）

> render task 的实现规范：它就是 `render` 协程本身——协程内部结构、逐帧录制循环、结束信号的来源、render context dump（未来向）。
> 关联：renderable 概念见 renderable.md；submit() 一帧编排与 render scheduler 见 render-scheduler.md；renderer 上下文见 renderer.md。

---

## 1. render task 的形态

一个 **render task** 就是 `t.render(renderer)` **本身**——renderable.md 那个接入面返回的协程（sender），跑在 render scheduler 上，代表「`t` 参与每一帧的绘制」。它**注册一次**，自己在内部完成 初始化 → 逐帧录制循环 → 收尾；render task 就等于 `render` 这个协程。

> `render` **只有一个参数 `renderer`**。现阶段 `renderer` 就是那个唯一的 renderer 实现，本质是一个保存 vulkan 上下文（instance / device / 当帧 command buffer 等）的 context。`render` 体内的**正确用法 = 直接调 vulkan 函数，把 renderer 携带的这些 handle 传进去**。整个游戏环境（结束信号、scheduler 那些设施）不放进这个参数位——那些从协程自身的 env 取（§2）；`t` 才是携带上下文的一方。用 `t`（而非 entity 本身）当主语，是为了不强求 `render` 必须由 entity 实现：entity 可以让它管理的另一个对象去实现 `render`。

### 常见形态：持续参与每一帧

render task 最常见的形态（伪代码；`render` 即 `t` 的 `render` 成员，函数本身就是协程）：

```cpp
auto T::render(renderer& r) -> render_task
{
    auto env  = co_await environment();
    auto stop = get_stop_token(env);      // 结束信号：恒从自身 env 取（§2）
    auto sched = /* t / context 携带的 render scheduler（现阶段临时从 context 取，见 render-scheduler/impl.md） */;

    // 初始化：GPU 资源作为协程帧内的局部量，用 RAII 拥有者持有（构造即建、析构即毁）。
    // 就是一串 vkCreate*，首参是 r 携带的 device；活过整个循环。
    auto pipeline = make_pipeline(r);
    auto vertices = make_buffer(r);
    // 只该建一次，不写 if (== VK_NULL_HANDLE) 守卫；要表达不变式用 assert。

    while (!stop.stop_requested())
    {
        co_await schedule(sched);         // 挂起，等下一帧 submit() 放行自己
        // 醒来时一定在「帧已开启」窗口内（render-scheduler.md 保证）：取 r 携带的当帧 command buffer，
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
- **初始化在循环前**：用 `renderer` 携带的 vulkan 上下文（device 等）建好自建资源，作为**协程帧内的局部量**持有（瞬态态 → 协程局部）。因为只建一次，不写 `if (handle == VK_NULL_HANDLE)` 初始化守卫；要表达"此处应为空"用 `assert`。
- **循环体每帧录一次**：`co_await schedule(sched)` 挂起到下一帧；醒来后调 `vkCmd*` 把 draw 录进 renderer 交给的当帧 command buffer。录进哪个 command buffer（primary / secondary）取决于并发模型，见 render-scheduler/model-ab.md。
- **清理在循环后**：观察到结束信号、退出循环，帧内局部量随协程帧析构自动释放——无需手动 teardown。

### 高度自定义留有的口子

这个形态只是**预设的常见写法**，不是约束：

- **一次性绘制**：可以不写循环，借别人的 pipeline 画一次就销毁——pipeline 仍归别人持有。
- **多个 render task**：一个 entity 可以在不同时机注册很多个 render task。绘制粒度落在 **render task** 上，不是 entity——这样「A 的剑要盖在更近的 B 前面」这种跨深度情形也能用多个 task 表达。
- **谁来实现 `render`**：可以是 entity 本身，也可以是 entity 管理的另一个对象。

> render task 怎么被注册（entity.main → render scheduler）见 boot.md。

---

## 2. 结束信号：恒从 env 取

结束信号通过**协程自己的 stop token** 传递，从 env 取，**不通过 context**。

- 协程在自己的 stdexec 环境（env）里查询 stop token：`stdexec::get_stop_token(env)`，循环条件就是 `!stop.stop_requested()`。
- stop token 沿调度链从上游传到协程的 env——上游（scheduler / scope / 父任务）持有 stop source，请求停止时，协程下次醒来即看到 `stop_requested() == true`，退出循环、跑清理、结束。
- 这样 render task 的「该退出了」是它**自己从环境读出来的**，而不是去翻一个共享的全局标志。各 task 互不依赖同一个全局标志，结束信号的来源是统一的、标准的。

> 调度器（`schedule` 的来源）本应同样从 env 取，但现阶段因转发调度器有妥协——见 render-scheduler/impl.md。

---

## 3. render context 可 dump（未来向）

`render` 依赖的 context 应可被 **dump**、也可从 dump 出的对象**重建**（服务快照 / 联网 / 离线恢复，呼应 ../engine-spec.md 英雄=协程 与快照）。

- 现阶段为控复杂度**不做**，但方向朝此走。
- 瞬态 GPU 资源作为协程帧局部量持有（§1），恰好使 dump / 恢复只需重建**耐久 context**——瞬态部分由重启协程自然重建。
