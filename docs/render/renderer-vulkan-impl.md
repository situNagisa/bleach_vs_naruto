# bvn 渲染·renderer 的 vulkan 实现（现状 + 实现）

> 本文讲 Vulkan 如何实现 renderer/env 边界：具体 vk* 命令序列落在 [renderer.md §2](renderer.md#2-帧生命周期五阶段规范级) 的五阶段职责上。
> 帧编排见 [render-scheduler.md](render-scheduler.md#bvn-渲染render-scheduler设计-规范)。

---

## 1. 当前实现状态

当前实现已经采用模型 B 的资源路径：

- render task 只拿 global env；
- render task 自己持有 `frames_in_flight` 份 secondary command pool/buffer；
- 每帧通过 `render_workflow::on_frame(secondary_pool, secondary_buffer)` 获取 frame env；
- 每个 task 录自己的 secondary command buffer；
- primary 按 waiter 顺序 `vkCmdExecuteCommands` join；
- dynamic rendering 使用 `VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT`。

现阶段仍在同一个内部 run loop 上顺序恢复 task；这只是执行资源妥协，不改变接口和资源归属。

---

## 2. 持久环境：`vulkan_context`

`vulkan_context` 拥有框架级 Vulkan 资源。

global 资源：

```cpp
VkInstance / VkDebugUtilsMessengerEXT / VkSurfaceKHR
VkPhysicalDevice / VkDevice
VkQueue graphics_queue, present_queue (+ family index)
VkSwapchainKHR + swapchain_images[] + swapchain_image_views[] + format + extent
VkSemaphore render_finished[]       // 每 swapchain image 一个
```

每个 in-flight frame slot：

```cpp
VkCommandPool   primary_command_pool
VkCommandBuffer primary_command_buffer
VkSemaphore     image_available
VkFence         in_flight
VkImage + VkImageView depth
```

每个 render task 自己持有：

```cpp
std::array<{
    VkCommandPool   secondary_command_pool;
    VkCommandBuffer secondary_command_buffer;
}, frames_in_flight>
```

---

## 3. 每帧环境：开帧 + 开 rendering

`render_workflow::submit()` 在所有 task 之前执行：

```cpp
// begin frame
vkWaitForFences(device, in_flight[cur])
vkAcquireNextImageKHR(device, swapchain, image_available[cur], &image_index)
vkResetFences(device, in_flight[cur])
vkResetCommandPool(device, primary_command_pool[cur])
vkBeginCommandBuffer(primary_command_buffer[cur])
```

然后做 layout barrier 并开启 dynamic rendering：

```cpp
swapchain_image: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
depth_image:     UNDEFINED -> DEPTH_ATTACHMENT_OPTIMAL

vkCmdBeginRendering(primary_cmd, VkRenderingInfo{
    flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT,
    color = { swapchain_view, loadOp=CLEAR, storeOp=STORE },
    depth = { depth_view,     loadOp=CLEAR, storeOp=DONT_CARE },
    renderArea = swapchain_extent,
})
```

`vkCmdBeginRendering` / `vkCmdEndRendering` 永远由 workflow 的 `submit()` 调用，render task 不碰。

---

## 4. 录制期：render task secondary

对每个等待中的 task，`submit()` 从 awaitable 读取 task 传入的 handles：

```cpp
auto pool = waiter->command_pool;
auto command = waiter->command_buffer;

vkResetCommandPool(device, pool);
vkBeginCommandBuffer(command, inheritance_for_dynamic_rendering);

auto frame = context.frame_env(slot_index, active_image_index, pool, command);
waiter->resume(dynamic_forward_frame_env_renderer(frame));

vkEndCommandBuffer(command);
```

task 侧只看到 frame env：

```cpp
auto command = frame.secondary_command_buffer();
vkCmdBindPipeline(command, ...);
vkCmdDraw(command, ...);
```

如果 task 使用第三方后端（例如 ImGui Vulkan backend），也把后端 draw data 录进同一条 secondary。

---

## 5. 结束一帧：execute secondaries + submit/present

所有 task 录完后：

```cpp
vkCmdExecuteCommands(primary_cmd, secondaries_in_waiter_order)
vkCmdEndRendering(primary_cmd)

swapchain_image: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR

vkEndCommandBuffer(primary_cmd)
vkQueueSubmit2(graphics_queue, {
    wait   = image_available[cur],
    cmd    = primary_cmd,
    signal = render_finished[image_index],
    fence  = in_flight[cur],
})
vkQueuePresentKHR(present_queue, { wait = render_finished[image_index], swapchain, image_index })
cur = (cur + 1) % frames_in_flight
```

`vkCmdExecuteCommands` 的数组顺序就是绘制顺序，当前按 waiter 顺序。

---

## 6. resize / OUT_OF_DATE

resize 或 swapchain out-of-date 时：

1. `vkDeviceWaitIdle(device)`；
2. 清 swapchain-sized 资源：swapchain image views、render-finished semaphores、depth images；
3. 重建 swapchain/images/views/render-finished；
4. 为每个 frame slot 重建 depth image/view；
5. bump swapchain revision。

secondary command buffers 不随 resize 重建；它们只录命令，下一帧 begin 时继承新的 dynamic rendering attachment format/extent。

---

## 7. 两个 concept 的访问器（Vulkan 实现）

`global_vulkan_env_renderer`：

```cpp
instance()                    -> VkInstance
physical_device()             -> VkPhysicalDevice
device()                      -> VkDevice
graphics_queue()              -> VkQueue
graphics_queue_family()       -> std::uint32_t
swapchain()                   -> VkSwapchainKHR
swapchain_extent()            -> VkExtent2D
swapchain_image_format()      -> VkFormat
swapchain_image_count()       -> std::uint32_t
swapchain_images()            -> span<VkImage const>
swapchain_image_views()       -> span<VkImageView const>
swapchain_render_finished()   -> span<VkSemaphore const>
depth_format()                -> VkFormat
device_name()                 -> char const*
```

`frame_vulkan_env_renderer`：

```cpp
in_flight()                   -> VkFence
image_available()             -> VkSemaphore
active_image_index()          -> std::uint32_t
secondary_command_pool()      -> VkCommandPool
secondary_command_buffer()    -> VkCommandBuffer
depth_image()                 -> VkImage
depth_image_view()            -> VkImageView
```

render task 常规只需要 `secondary_command_buffer()`；primary command buffer 留在 workflow 内部，不通过 frame env 暴露。

---

## 8. 转发实现

`renderer.h` 提供两组独立转发：

```cpp
global_forward_env_renderer<T>
frame_forward_env_renderer<T>

global_dynamic_forward_env_renderer
frame_dynamic_forward_env_renderer
```

dynamic forward 是 move-only 类型擦除壳，内部 `unique_ptr<basic_...>`；便利函数只做单侧包装：

```cpp
dynamic_forward_global_env_renderer(context.global_env())
dynamic_forward_frame_env_renderer(context.frame_env(slot, active_image, secondary_pool, secondary_cmd))
```

没有合体 `renderer` concept，也没有同时包 global + frame 的“大 renderer 参数”。

---

## 9. 结束全部（关机）

关机次序：

1. 请求 render tasks 停止；
2. 唤醒等待 `on_frame(...)` 的 tasks；
3. 排空 render/main scopes；
4. `vkDeviceWaitIdle(device)`；
5. task 自建资源析构；
6. `vulkan_context` 清 frame slots、swapchain resources、device、surface、debug messenger、instance。

device 必须晚于所有依赖它的资源销毁。
