# 任务 02：render scheduler 模型 B（已并入 graphics 主线）

> 这份 tmp 任务原本描述“在任务 01 之后再迁移模型 B”的旧执行方案。当前实现已经把 renderer env 二分与模型 B 一起落到 `graphics` 主线；本文只保留落地后的事实，避免继续传播旧的 `forward_renderer` / 合体 renderer / task index 草案。

---

## 1. 当前事实

- `render(...)` 只接收 `global_dynamic_forward_env_renderer`。
- render task 在循环里 `co_await render_workflow->on_frame(pool, buffer)`，每帧取得 `frame_dynamic_forward_env_renderer`。
- `pool / buffer` 是该 render task 自己持有的 secondary command pool / command buffer；通常每 task 准备 `frames_in_flight` 份，按帧轮转传入。
- frame env 只暴露当前 task 的 secondary command pool / command buffer，以及本帧 image/fence/depth 等必要句柄；primary command buffer 留在 workflow 内部。
- `vulkan_context` 拥有 Vulkan global 与 frame slot 资源，不拥有 per-task secondary 资源。
- `submit()` 摘取 waiter vector 后，逐个 reset/begin task 传入的 secondary、resume task 录制、end secondary，再按同一顺序 `vkCmdExecuteCommands`。
- workflow 只在本帧栈上临时收集 `std::vector<VkCommandBuffer>`，用完即丢。

## 2. 关键语义

- 绘制顺序 = 本帧 waiter vector 顺序。`submit()` 把 waiter continuation 放到内部 scheduler 上恢复；task 录完后下一轮再调用 `on_frame(pool, buffer)` 等待下一帧。当前 task 按启动顺序进入等待，因此 waiter 顺序随启动顺序稳定。
- 零尺寸 / out-of-date 等暂时没有可画帧的状态不 stop render task，只保留 waiter 等下一次可提交帧。
- shutdown / workflow stop_source 请求停止时恢复等待中的 awaitable；task 恢复后观察自己的 stop token 并退出。
- 当前实现假设 task 从拿到 frame env 到再次挂起之间同步录制，不在 secondary recording 区间 `co_await` 其他异步工作。

## 3. 权威文档

- [../render/renderer.md](docs/render/renderer.md)
- [../render/renderer-vulkan-impl.md](../render/renderer-vulkan-impl.md)
- [../render/render-task.md](docs/render/render-task.md)
- [../render/render-scheduler.md](docs/render/render-scheduler.md)
- [../render/render-scheduler/impl.md](../render/render-scheduler/impl.md)
- [../render/render-scheduler/model-ab.md](docs/render/render-scheduler/model-ab.md)
