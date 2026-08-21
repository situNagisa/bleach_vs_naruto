# 任务 01：renderer 抽象拆分（global env / frame env）

> 本文记录本次落地的真实主线。旧版把 `renderer` 设计成 `global_env && frame_env` 的合体参数，并试图把每帧环境随同一个 renderer 传进长期运行的 render task；这个方向不成立。当前代码以 `include/bvn/graphics/renderer.h`、`include/bvn/graphics/vulkan_renderer.h` 和 `source/client/render_workflow.h` 为准。

---

## 1. 原问题

render task 是长生命周期协程：`entity.main()` 只 spawn 一次 `render(...)`，之后协程自己循环参与每一帧。

如果 `render(...)` 的参数是一个“完整 renderer”，那么这个参数只能在 spawn 时绑定一次。可是 frame env 的核心数据（本帧 image index、帧槽、尤其是本 task 当帧要录入的 secondary command buffer）每帧都会变化，无法靠 spawn 参数每帧注入。

所以合体模型的问题不是接口宽窄，而是生命周期错位：

- global env：整段 render task 生命周期都稳定，适合做 `render(...)` 参数。
- frame env：只在某一帧、某个 task 被放行录制时有效，必须由每帧 await 返回。

---

## 2. 当前解法

`render(...)` 只接收 global env：

```cpp
auto render(::bvn::graphics::global_dynamic_forward_env_renderer global)
    -> ::bvn::gameplay::task;
```

每个 render task 自己持有 `frames_in_flight` 份 secondary command pool / command buffer：

```cpp
auto secondaries = create_render_task_secondary_commands(global);
auto secondary_slot_index = ::std::uint32_t{};
```

循环里每帧把当前 secondary handles 交给 workflow，领取 frame env：

```cpp
while (!stop.stop_requested())
{
    auto&& secondary = secondaries[secondary_slot_index];
    auto frame = co_await render_workflow->on_frame(secondary.pool.handle, secondary.buffer);
    secondary_slot_index = (secondary_slot_index + 1) % ::bvn::graphics::frames_in_flight;

    if (stop.stop_requested())
    {
        break;
    }

    auto command = frame.secondary_command_buffer();
    // vkCmd* ...
}
```

`on_frame(pool, buffer)` 是 coroutine awaitable，不是 sender。shutdown 时 workflow 只负责恢复等待中的协程；协程恢复后观察自己的 stop token 并退出。零尺寸 / swapchain 暂不可用只是临时无帧：workflow 保留 waiter vector，等下一次有效 `submit()` 再返回 frame env。

---

## 3. renderer.h 边界

`include/bvn/graphics/renderer.h` 定义两个独立 concept：

- `global_env_renderer`：instance / physical device / device / queue / swapchain / attachment format / device name 等初始化期资源。
- `frame_env_renderer`：当前帧同步对象、当前 task 的 secondary 命令资源、depth image/view 等录制期资源。primary 命令资源留在 workflow 内部，不暴露给 render task。

不再定义：

```cpp
renderer = global_env_renderer && frame_env_renderer
```

转发壳分两类：

- `global_forward_env_renderer<T>` / `frame_forward_env_renderer<T>`：模板静态转发，保留 `T` 的 value / pointer / reference 语义，不做 dispatch，不加额外 public `get()`。
- `global_dynamic_forward_env_renderer` / `frame_dynamic_forward_env_renderer`：move-only，内部 `unique_ptr<basic_...>`，用虚函数做类型擦除。动态 eraser 按值拥有 `remove_cvref_t<R>`，避免 frame payload 绑定到短命栈对象。

便利函数也只做单侧 dynamic forward：

```cpp
dynamic_forward_global_env_renderer(...)
dynamic_forward_frame_env_renderer(...)
```

---

## 4. Vulkan 实现边界

`include/bvn/graphics/vulkan_renderer.h` 新的核心类型是 `vulkan_context`。

`vulkan_context` 统一拥有：

- Vulkan global 资源：instance / surface / physical device / device / queue / swapchain / image views。
- frame slot 资源：primary command pool/buffer、image-available semaphore、in-flight fence、depth image。

secondary command pool / command buffer 不归 `vulkan_context` 拥有；它们由 render task 自己持有。workflow 只在 `submit()` 中拿到本帧 task 传入的 handles，reset / begin / end / execute，用完即丢。

两个 env view/payload：

- `global_vulkan_env_renderer`：持 `vulkan_context const*`，由 `vulkan_context::global_env()` 产出。
- `frame_vulkan_env_renderer`：持 `vulkan_context const*`、slot index、active image index，以及本帧本 task 传入的 secondary pool/buffer，由 `vulkan_context::frame_env(slot, active_image, pool, buffer)` 产出。slot / active image 是 `render_workflow` 的当帧动态状态，不存进 `vulkan_context`。

`resize(vulkan_context&, window_extent)` 是自由函数。初始化代码仍由使用处内联完成，析构由 `vulkan_context` 自己负责。

---

## 5. render workflow 模型 B

`render_workflow` 暴露：

```cpp
auto query(::stdexec::get_scheduler_t) -> inner_static_thread_pool_scheduler;
auto on_frame(VkCommandPool pool, VkCommandBuffer buffer) -> on_frame_awaitable;
void submit();
```

- `get_scheduler(render_workflow)` 只是透出内部 `exec::static_thread_pool` scheduler，用来启动长期 render task；它不是 workflow 自己实现的一套 scheduler。
- `on_frame_awaitable` 由 `nagisa::concurrency::build_awaitable_t` 组装，不是 sender，也不是 task。
- 没有 task index、注册表、旧 sender 队列状态或同步等待辅助接口。
- 当前绘制顺序就是本帧 waiter vector 的顺序；`submit()` 把 waiter continuation 放到内部 scheduler 上恢复，task 录完后下一轮再调用 `on_frame(pool, buffer)` 等待下一帧。当前 task 按启动顺序进入等待，因此 waiter 顺序随启动顺序稳定。

`render_workflow::submit()` 每帧做：

1. 处理 resize / acquire / fence / reset primary pool。
2. `vkCmdBeginRendering(... VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT ...)`。
3. 取出 waiter vector。
4. 创建栈上临时 `std::vector<VkCommandBuffer> secondaries`。
5. 对每个 waiter：
   - 读取 task 传入的 secondary pool / command buffer；
   - reset secondary pool；
   - begin secondary command buffer，带 dynamic rendering inheritance；
   - 构造 `frame_dynamic_forward_env_renderer` 并在内部 scheduler 上恢复 task 录制；
   - task 返回后 end secondary；
   - 把 secondary command buffer push 进本帧临时 vector。
6. primary 按 waiter 顺序 `vkCmdExecuteCommands(secondaries)`。
7. end rendering / barrier to present / submit / present / 轮转 frame slot。

---

## 6. entity 迁移口径

`arena` / `hero` / `debug_overlay`：

- include 从旧显示架构头迁到 `graphics`。
- `render(...)` 签名改为只收 `global_dynamic_forward_env_renderer global`。
- 初始化资源只用 `global.device()`、`global.physical_device()`、`global.swapchain_image_format()` 等。
- render task 自己创建 `frames_in_flight` 份 secondary command pool / command buffer。
- 每帧 `auto frame = co_await render_workflow->on_frame(pool, buffer);`。
- draw 命令全部录进 `frame.secondary_command_buffer()`。

---

## 7. 验收

- Debug x64 编译通过。
- shutdown 时等待中的 `on_frame(...)` awaitable 能被恢复，task 观察 stop token 后退出；临时无帧不终止长期 render task。
- 冒烟运行：网格、英雄动画、overlay 正常；resize、F1、关窗退出正常。
- validation layer 无报错。
- 模型 B 检查：每 task 使用自己的 secondary，primary 按本帧 waiter 顺序 `vkCmdExecuteCommands`。
