# bvn Vulkan 概念问答（Q&A）

> 日期：2026-06-28　状态：**学习记录 · 非规范**
> 设计 render scheduler 时澄清的 Vulkan 基础概念。**规范以 `renderer.md` 为准**；本文是其概念背景与示例，便于回顾。

---

## 0. 大前提：GPU 是异步的

CPU 把命令录进 command buffer、`vkQueueSubmit` 提交；GPU **稍后**按自己的时钟执行，CPU 默认**不等**。下面 in-flight / fence / semaphore / 帧槽 全是为了协调「两个跑在不同速度上的处理器」——记住这句，其余都是推论。

## 1. in-flight：是什么、为什么需要

**in-flight = 已提交给 GPU、但还没执行完的工作**（还在空中飞）。要管它，因为两件事：

**(a) 资源复用危险。** command buffer 及它引用的数据（顶点、uniform）在 GPU 执行期间**正被 GPU 读**。若 CPU 为下一帧、在 GPU 还没读完时就**重录这同一个 CB**，等于改了 GPU 正在用的东西 → 画面错乱 / 崩。所以 CPU 必须能问「这 CB 的 GPU 活干完没？」——这就是 **fence**。

**(b) 吞吐（真正的动机）。** 若 CPU 提交完第 N 帧就**干等** GPU 画完再开始第 N+1 帧，则 CPU、GPU 永远一忙一闲，白扔一半性能。我们要 CPU 在 GPU 还画着第 N 帧时就准备第 N+1 帧 = **流水线**。但不能无限领先（延迟爆炸、要无限缓冲），于是设上限：**CPU 最多领先 GPU N 帧**，N = 最大 in-flight 帧数，常取 **2**。安全流水线需要 N 份互不踩的「每帧资源」→ 帧槽。

## 2. 帧槽（frame slot）

**帧槽 = 渲染一帧所需「每帧资源」的一份完整拷贝**：一个 command pool + primary CB、`image_available` / `render_finished` 两个 semaphore、一个 `in_flight` fence、（模型 B）每录制线程一个 secondary 池、以及任何「CPU 每帧写、GPU 每帧读」的 GPU buffer。

做 **N 份**（N=2）。第 k 帧用 `slot = k % N`。GPU 用 slot0 画第 N 帧时，CPU 用 slot1 准备第 N+1 帧——两块独立内存不冲突。复用某槽前先等它的 fence。

> **关键澄清（高频坑）：帧槽 ≠ swapchain 图像。** swapchain 可能 3 张图，帧槽可能 2 个，两者解耦：画到**哪张 swapchain 图**由 `vkAcquireNextImageKHR` 每帧轮换决定；用**哪个帧槽**由 `帧号 % N` 决定。混淆二者即 `renderer.md §3.4`「`render_finished` 应每 swapchain image 一份」那条注意的根源。

**fence vs semaphore**（in-flight 离不开它俩）：

| | 通知方向 | 谁在等 |
|---|---|---|
| **fence**（`in_flight`） | GPU → **CPU** | CPU 用 `vkWaitForFences` 等 |
| **semaphore**（`image_available` / `render_finished`） | GPU → GPU（队列内部排序） | CPU 看不见其值，只把它挂在 submit / present 上 |

## 3. dynamic rendering vs 老用法

**老 Vulkan（1.0–1.2）** 画东西前必须先建两个「重」对象：

- **`VkRenderPass`**：静态描述所有附件、各自 load/store、subpass 结构。开局建。
- **`VkFramebuffer`**：把具体 image view 绑到 render pass。开局建，resize 重建。

啰嗦死板：每种附件组合一个 render pass，framebuffer 要匹配，swapchain 一变一堆样板。

**dynamic rendering**（`VK_KHR_dynamic_rendering`，Vulkan **1.3 转正**）：跳过这俩。录制时直接 `vkCmdBeginRendering(cmd, VkRenderingInfo{ 附件内联 })`，`vkCmdEndRendering` 收尾。简单灵活，是当前新代码推荐做法。

**能完全取代吗？对 Windows 桌面——基本能，大概一辈子不碰 `VkRenderPass`。** 两个例外：

- **subpass / input attachment**：给手机 tile GPU 省带宽（附件留片上 tile 内存里跨 pass）；桌面 GPU 基本不受益，真要类似能力有单独扩展（`local_read`）。用不上。
- **硬件**：要 Vulkan 1.3（或扩展），2020 年后 GPU 都有。

dynamic rendering **支持 secondary command buffer**（靠 `VkCommandBufferInheritanceRenderingInfo`），故与模型 B 不冲突。

## 4. 深度图 hazard（为什么要每帧槽一份）

**深度图** = 全屏图，每像素存「已画过的最近物体深度」。深度测试：画新像素时比深度，**更远就丢弃**（被挡）→ 3D 自动遮挡，不必手动排序。

**hazard**：只有一张深度图，却最多 2 帧 in-flight。GPU 画第 N 帧（读写这唯一深度图）时，CPU 已开始第 N+1 帧、会去 **CLEAR 并写同一张** → 两帧在同一块深度内存上打架 = **数据竞争**。

为什么**颜色图没事**？swapchain 每帧给不同图（2–3 张轮换）；深度图却是你自建的唯一一张、被共享了。fence 也救不了（它只防「第 N+2 帧复用 slot0 前等第 N 帧」，不防 N、N+1 在 GPU 上重叠共用深度图）。

**解法：每帧槽一张深度图（N 张）。** 1080p D32 约 8MB，×2≈16MB，可忽略。（sync validation 会报这个，值得主动修。）

## 5. 模型 B 示例代码（示意）

> 示意用：真实 Vulkan 调用、去掉错误处理；`frame_slot` / `task_recorder` 为示意名，非项目类型。

```cpp
// ── 每个帧槽的资源（N=2，做两份）──────────────────────────────
struct frame_slot
{
    VkCommandPool   primary_pool;     // 主 CB 的池
    VkCommandBuffer primary_cmd;      // 这一槽专用的主指令缓冲
    VkSemaphore     image_available;  // acquire 完成时 GPU signal（可往这张图画了）
    VkSemaphore     render_finished;  // 渲染完成 signal，gate present
    VkFence         in_flight;        // 这一槽的 GPU 活干完了吗（CPU 等它）
    VkImage         depth_image;      // ★ 每槽一份深度图（修掉 §4 的 hazard）
    VkImageView     depth_view;
};
frame_slot slots[2];
uint32_t   frame_index = 0;

// ── 一个“绘制任务”自带的录制器（可丢到 worker 线程并行录）────────
struct task_recorder
{
    VkCommandPool   pool;             // ★ 每个录制线程一个池（池是外部同步的）
    VkCommandBuffer secondary;        // 录到这里，最后交回主流程
};
```

```cpp
// ── 录一个 task 的 secondary：这一步可以并行 ─────────────────────
void record_secondary(task_recorder& rec, VkFormat color_fmt, VkFormat depth_fmt /*, 要画什么 */)
{
    // dynamic rendering 下，secondary 必须先“继承”它将渲染到的附件格式
    VkCommandBufferInheritanceRenderingInfo rinfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &color_fmt,
        .depthAttachmentFormat   = depth_fmt,
        .rasterizationSamples    = VK_SAMPLE_COUNT_1_BIT,
    };
    VkCommandBufferInheritanceInfo inherit{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO, .pNext = &rinfo };
    VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT   // “我是在某个 rendering 内部续录的”
               | VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = &inherit };

    vkBeginCommandBuffer(rec.secondary, &begin);
    // —— 这里全是 renderable 自己的事，怎么画都行 ——
    vkCmdSetViewport(rec.secondary, 0, 1, &viewport);
    vkCmdSetScissor (rec.secondary, 0, 1, &scissor);
    vkCmdBindPipeline(rec.secondary, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindVertexBuffers(rec.secondary, 0, 1, &vbo, &offset);
    vkCmdBindDescriptorSets(rec.secondary, /*...*/ &texture_set, /*...*/);
    vkCmdPushConstants(rec.secondary, /*...*/ &transform);
    vkCmdDraw(rec.secondary, 6, 1, 0, 0);
    vkEndCommandBuffer(rec.secondary);
}
```

```cpp
// ── 一帧主流程（单线程 join）────────────────────────────────────
void render_frame(std::vector<task_recorder>& tasks)
{
    frame_slot& s = slots[frame_index % 2];                 // ★ 选帧槽

    // 1) 等这一槽上轮 GPU 活干完，才能复用它的 CB / depth
    vkWaitForFences(device, 1, &s.in_flight, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &s.in_flight);

    // 2) 要画到哪张 swapchain 图？（每帧轮换，和帧槽是两回事）
    uint32_t img;
    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, s.image_available, VK_NULL_HANDLE, &img);

    // 3) 开录主 CB
    vkResetCommandPool(device, s.primary_pool, 0);
    VkCommandBufferBeginInfo bi{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                 .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(s.primary_cmd, &bi);

    // 4) 布局转换（VkImageMemoryBarrier2）：让图可写
    transition(s.primary_cmd, swap_images[img], UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL);
    transition(s.primary_cmd, s.depth_image,    UNDEFINED -> DEPTH_ATTACHMENT_OPTIMAL);

    // 5) 开 dynamic rendering —— 关键：声明内容来自 secondary
    VkRenderingAttachmentInfo color{ .imageView = swap_views[img],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {背景色} };
    VkRenderingAttachmentInfo depth{ .imageView = s.depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = { .depthStencil = { 1.0f, 0 } } };
    VkRenderingInfo ri{ .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT,   // ★ 我要用 secondary
        .renderArea = {{0,0}, extent}, .layerCount = 1,
        .colorAttachmentCount = 1, .pColorAttachments = &color, .pDepthAttachment = &depth };
    vkCmdBeginRendering(s.primary_cmd, &ri);

    // 6) ★ 并行录所有 secondary（这层 for 就是 sender 模型里 when_all 的位置）
    std::vector<VkCommandBuffer> recorded;
    for (auto& t : tasks) {                  // ← 可丢到多线程并行
        record_secondary(t, swap_format, depth_format /*...*/);
        recorded.push_back(t.secondary);
    }

    // 7) ★ join：把所有 secondary 按数组顺序塞进主 CB 执行
    //    数组顺序 == 绘制先后（现阶段就用提交顺序，排序以后再说）
    vkCmdExecuteCommands(s.primary_cmd, (uint32_t)recorded.size(), recorded.data());

    vkCmdEndRendering(s.primary_cmd);

    // 8) 转成可呈现布局，收尾主 CB
    transition(s.primary_cmd, swap_images[img], COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR);
    vkEndCommandBuffer(s.primary_cmd);

    // 9) 提交：等图像就绪 → 跑主 CB → 完事 signal render_finished + fence
    VkSemaphoreSubmitInfo     wait  { .semaphore = s.image_available,
                                      .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphoreSubmitInfo     sig   { .semaphore = s.render_finished,
                                      .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkCommandBufferSubmitInfo cmd   { .commandBuffer = s.primary_cmd };
    VkSubmitInfo2 submit{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1, .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1, .pCommandBufferInfos = &cmd,
        .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &sig };
    vkQueueSubmit2(graphics_queue, 1, &submit, s.in_flight);   // fence 在这帧 GPU 干完时 signal

    // 10) 呈现：等 render_finished → 把这张 swapchain 图显示出去
    VkPresentInfoKHR present{ .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &s.render_finished,
        .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &img };
    vkQueuePresentKHR(present_queue, &present);

    frame_index++;     // 下一帧换另一槽
}
```

## 6. 接到 sender / 协程模型

- 第 1–5 步（每帧环境）= 主线程，task 之前；
- 第 6 步那层 for（并行录各自 secondary）= **`when_all`** 的位置——真正的并发录制在这里；
- 第 7–10 步（`vkCmdExecuteCommands` → submit → present）= **join 之后**，回主线程串行。

**示意省略**：错误处理、resize / OUT_OF_DATE 重建、`transition()` 的 barrier 细节、`render_finished` 每 swapchain image 一份。

---

## 7. 与规范文档的关系

- 帧生命周期五阶段、并发模型 A/B、scheduler 契约 → `renderer.md §3 / §4 / §5`。
- renderable 侧的约定（只认 `cmd`、不碰帧结构、顺序归后端）→ `display-architecture.md §6`。
