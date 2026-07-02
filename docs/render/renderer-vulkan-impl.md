# bvn 渲染·renderer 的 vulkan 实现（现状 + 实现）

> 本文讲 vulkan 如何实现 renderer：具体 vk* 命令序列，落在 renderer.md 定的五阶段职责上。
> vulkan 基础概念（in-flight / 帧槽 / dynamic rendering / 深度图 hazard）见 vulkan-qa.md；帧开闭由谁驱动见 render-scheduler.md。

---

## 1. 现状：初期用 vulkan，后期萃取规范

`renderer` 本身足够复杂（参考 `vulkan` / `opengl` 的设计都很大一坨），**凭空设计一个足够优秀的渲染器是不可能的**。

为了保证开发推进，现阶段的 `renderer` 直接用 `vulkan`——即**把 `vulkan` 当做 `renderer` 规范**。我们写的不是 vulkan，而是 `renderer` 规范，以此限定 vulkan 的使用范围（使用 vulkan 的地方不能超过 renderer 的界定，利将来 CUDA interop；不上 bindless / render-graph）。待开发到一定程度，再从这里萃取出稳定的 renderer 规范。

**A↔B 现状**：模型 A（单 primary command buffer、串行录制）是**当前实现**；模型 B（每 task 自带 secondary、并行录制 + `vkCmdExecuteCommands` join）是**选定的演进方向**（见 render-scheduler/model-ab.md）。下面的命令序列描述当前模型 A。

> 命令序列为**规范级**（实现以此为准）；现阶段基于 Vulkan 1.3 **dynamic rendering**（无 `VkRenderPass` / `VkFramebuffer` 对象）。

---

## 2. 持久环境（renderer 一次性建立，整程序持有）

renderable **只读不建**，renderer 持有以下持久数据（**含全部 in-flight 帧槽**）：

```cpp
VkInstance / VkSurfaceKHR / VkPhysicalDevice / VkDevice
VkQueue graphics_queue, present_queue (+ family index)
VkSwapchainKHR + swapchain_images[] + image_views[] + format + extent
VkSemaphore render_finished[]       // 每 swapchain image 一个，渲染完成、gate present
```

每个 in-flight 帧槽一组（帧槽数现取 2）：

```cpp
VkCommandPool   command_pool        // 每槽一个，整池 reset
VkCommandBuffer command_buffer      // primary，从该池分配
VkSemaphore     image_available     // acquire 完成时 signal
VkFence         in_flight           // 本槽 GPU 是否干完
VkImage + VkImageView depth         // 每槽一份深度图，D32_SFLOAT
```

深度图按帧槽持有，避免 N+1 帧清深度撞上 N 帧仍在读的 hazard（见 vulkan-qa.md 深度 hazard）。

---

## 3. 每帧环境（开帧 + 开 rendering，在所有 task 之前）

由 render scheduler 的 `submit()` 在所有 task 之前执行——以 `//` 注释 + `{}` 块**内联**（每帧只有这一处调用，故不做具名函数），操作 renderer 持有的 vulkan 数据：

```cpp
// begin frame：开帧（acquire + reset + 开 primary CB）
{
    vkWaitForFences(in_flight[cur])
    vkAcquireNextImageKHR(swapchain, image_available[cur], &image_index)   // OUT_OF_DATE → 重建 swapchain；SUBOPTIMAL → 本帧继续、请求下帧重建
    vkResetFences(in_flight[cur])    // acquire 成功后再 reset，避免 stale swapchain 时 fence 被错误清掉
    vkResetCommandPool(command_pool[cur])
    vkBeginCommandBuffer(primary_cmd)
    // 得到本帧句柄：{ primary CB, image_index, swapchain image / view, extent }
}
```

```cpp
// begin rendering：开 dynamic rendering（紧接开帧，仍在 submit() 内）
{
    // 1) 布局转换（sync2 / vkCmdPipelineBarrier2）
    swapchain_image: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    depth_image:     UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL
    // 2) 开 dynamic rendering
    vkCmdBeginRendering(primary_cmd, VkRenderingInfo{
        color = { swapchain_view, loadOp=CLEAR, storeOp=STORE, clear=背景 },
        depth = { depth_view,     loadOp=CLEAR, storeOp=DONT_CARE, clear=1.0 },
        renderArea = 全 extent,
        flags = 0,   // 当前模型 A；模型 B 落地后改为 SECONDARY_COMMAND_BUFFERS_BIT
    })
}
```

**契约：`vkCmdBeginRendering` / `EndRendering` 永远由 render scheduler 的 `submit()` 执行，render task 绝不自调**；task 只在已开启的 rendering 实例内录 draw。

---

## 4. 录制期（render task）

单个 task 的命令清单（首参皆 `cmd`）：

```cpp
vkCmdBindPipeline(cmd, GRAPHICS, pipeline)
vkCmdSetViewport / vkCmdSetScissor(cmd, ...)
vkCmdBindVertexBuffers / vkCmdBindIndexBuffer(cmd, ...)
vkCmdBindDescriptorSets(cmd, ...)
vkCmdPushConstants(cmd, ...)
vkCmdDraw / vkCmdDrawIndexed(cmd, ...)
```

这里的 `cmd` 当前是 primary（模型 A）；模型 B 落地后会变成 task 自己的 secondary。

第三方渲染后端入口（例如 ImGui Vulkan backend）可以由 renderable 直接调用，只要它同样只把 draw 命令录进这个 `cmd`，并且不接管 `vkCmdBeginRendering` / `vkCmdEndRendering` / submit / present。项目不为这类三方库再包一层。

---

## 5. 结束一帧（收 rendering + 收帧）

本帧全部 task 录完后，`submit()` 收这一帧——同样以 `//` + `{}` 内联块执行：

```cpp
// end rendering：收 dynamic rendering
{
    vkCmdEndRendering(primary_cmd)
}

// end frame：转呈现布局 + 收 CB + 提交 + 呈现 + 轮转帧槽
{
    barrier: swapchain_image  COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
    vkEndCommandBuffer(primary_cmd)
    vkQueueSubmit2(graphics_queue, {
        wait   = { image_available[cur], stage=COLOR_ATTACHMENT_OUTPUT },
        cmd    = primary_cmd,
        signal = { render_finished },
        fence  = in_flight[cur],
    })
    vkQueuePresentKHR(present_queue, { wait={render_finished}, swapchain, image_index })   // OUT_OF_DATE / SUBOPTIMAL → 重建
    cur = (cur + 1) % 帧槽数
}
```

要点：
- **submit 是每帧的 join 点**：模型 A 下本帧所有 task 已按 FIFO 录进 primary command buffer 后才能提交；模型 B 落地后则是在 secondaries 已 `vkCmdExecuteCommands` 后提交。
- `wait` 阶段设 `COLOR_ATTACHMENT_OUTPUT`，让顶点等早期阶段与 acquire 重叠。
- `render_finished` 严格应**每 swapchain image 一个**（避免呈现期被复用）——小项目易忽略，记一笔。
- **resize / OUT_OF_DATE**：`wait_idle` → 重建 swapchain / views / depth。

---

## 6. 结束全部（关机）

次序为硬约束（GPU 在用的不能毁、device 不能先于其资源毁）：

```cpp
1. running=false → 排空 render scope（确保无 task 在录 / 在飞）
2. vkDeviceWaitIdle(device)
3. 各 renderable 析构 → 毁自建的 pipeline / buffer / image / descriptor pool / sampler / shader module（未来模型 B：含 secondary 池）
4. renderer 毁自有 per-frame / swapchain 资源：depth_image（view → image → memory）、command_pool ×N、image_available / render_finished / in_flight、swapchain_image_views[] → swapchain；这些都在 device idle 之后，内部只需满足对象自身依赖。
5. device → surface → debug_messenger → instance
```

**第 1→3 步次序对协程架构是硬约束**：render scope 先排空、renderable 先析构，renderer 才能拆，否则 renderable 持有的 handle 在 device 销毁后变野指针（即先排空全部渲染任务、再 device wait idle、最后拆 renderer）。运行时怎么编排这套收尾见 render-scheduler.md。
