# bvn 渲染器（renderer）· 设计方向

> 日期：2026-06-25　状态：**方向已定**
> 本文记录「渲染器」的设计思想，**用于描述项目、非实现计划**。参考 `engine-spec.md §4.5`（英雄产出渲染任务）的思路。
> 本文讲「一帧的 Vulkan 生命周期与并发模型」；这些帧步骤在运行时由谁、在什么时机驱动，见 `render-runtime.md`。
> Vulkan 基础概念（in-flight / 帧槽 / dynamic rendering / 深度图 hazard / 模型 B 示例）见 `vulkan-qa.md`。
>
> **A↔B 现状**：模型 A（单 primary command buffer、串行录制）是**当前实现**；模型 B（每 task 自带 secondary、并行录制 + `vkCmdExecuteCommands` join）是**选定的演进方向**。`render-runtime.md` 描述的运行时编排建立在当前的模型 A 之上。

---

## 1. 核心哲学：初期使用vulkan，后期慢慢萃取出规范

`renderer`本身足够复杂（参考`vulkan`，`opengl`的设计都很大一坨），**所以妄图凭空设计一个足够优秀的渲染器是不可能的**。
为了保证开发，我们初期的`renderer`用`vulkan`，即将`vulkan`当做我们的`renderer`规范，我们写的不是vulkan，而是`renderer`规范，以此来限定`vulkan`的使用范围。

------

## 2. renderer 的形态与职责划分

### 2.1 初始化

游戏主体在合适时机初始化 renderer。现阶段只有一个实现，所以不再抽象 `rhi` / `context` 层；renderer 自己持有 instance / device / swapchain 等框架级状态（完整清单见 §3.1）。renderable 直接读取这些 Vulkan handle，并用 Vulkan API 创建自己的 pipeline / buffer / texture 等 GPU 资源——怎么定 vertex 格式、要不要透明混合（2D sprite 的核心需求），全是它自己的事。

> 一个 renderable 初始化并录制自己资源的**完整示例**见 `vulkan-qa.md §5`。

### 2.2 绘制期

renderable 在 `render` 里录制自己的 draw 命令（单个 task 的命令清单见 §3.3）。当前模型 A 下它录进 renderer 的 primary command buffer；模型 B 落地后改为录进自己的 secondary command buffer、每帧录完交回 renderer 统一执行（见 §4.2）。两种模型下它都只认交给它的 command buffer，不碰帧结构（begin / end rendering）与 submit / present。

### 2.3 renderer 持有什么

renderer 作为持久对象，管理所有框架级 Vulkan 状态——instance / surface / device / queue、swapchain 及其 image / view、深度图、以及每个 in-flight 帧槽的命令池 / 命令缓冲 / 同步对象。完整清单与含义见 §3.1。

### 2.4 renderer 给 renderable 的接口边界

renderer **不包装资源创建**，避免做成第二套 Vulkan API：renderable 读取 renderer 里的 Vulkan 数据、直接调用 Vulkan。renderer 不为 renderable 增加辅助函数层；现阶段只暴露框架级 Vulkan 状态与帧生命周期入口。

### 2.5 渲染目标边界（现阶段约定）

renderer 决定渲染目标（attachment）结构（初期：单 color attachment + 单 depth，dynamic rendering，无多 pass / MSAA）；renderable 在此框架内自由决定 pipeline 的其余所有状态。需要多 pass 效果（描边、后处理）时再扩展 renderer 暴露相应能力。

---

## 3. 帧生命周期（五阶段）

> 按「持久环境 → 每帧环境 → 录制 → 结束一帧 → 结束全部」五段描述。命令序列为**规范级**（实现以此为准）；现阶段基于 Vulkan 1.3 **dynamic rendering**（无 `VkRenderPass` / `VkFramebuffer` 对象）。
> 概念背景（in-flight / 帧槽 / dynamic rendering / 深度图 hazard）见 `vulkan-qa.md §1–§4`。

贯穿全篇的**铁律**：

> `VkCommandBuffer` 及其所属 `VkCommandPool` 是**外部同步**对象——**同一个 command buffer 不能被并发录制**（同一时刻只能有一个执行体在录）；`VkQueue` 的提交 / 呈现亦然。这条铁律单独决定了并发模型（§4）。

### 3.1 持久环境（renderer 一次性建立，整程序持有）

renderable **只读不建**，renderer 持有：

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

**frames-in-flight 的含义与代价**：让 CPU 录第 N+1 帧时 GPU 仍在跑第 N 帧。代价——任何「CPU 每帧写、GPU 每帧读」的资源（uniform / 动态顶点）须备 **N 份**，否则跨帧读写相撞。

深度图按帧槽持有，避免 N+1 帧清深度撞上 N 帧仍在读的 hazard。

### 3.2 每帧环境（`begin_frame` + `begin_rendering`，在所有 task 之前）

renderer 每帧替 task 铺好台子。

`begin_frame()`：

```cpp
vkWaitForFences(in_flight[cur])
vkAcquireNextImageKHR(swapchain, image_available[cur], &image_index)   // OUT_OF_DATE → 重建 swapchain；SUBOPTIMAL → 本帧继续、请求下帧重建
vkResetFences(in_flight[cur])    // acquire 成功后再 reset，避免 stale swapchain 时 fence 被错误清掉
vkResetCommandPool(command_pool[cur])
vkBeginCommandBuffer(primary_cmd)
// 得到本帧句柄：{ primary CB, image_index, swapchain image / view, extent }
```

`begin_rendering(frame)`：

```cpp
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
```

**契约：`vkCmdBeginRendering` / `EndRendering` 永远归 renderer，render task 绝不自调**；task 只在已开启的 rendering 实例内录 draw。

### 3.3 录制期（render task）—— 详见 §4 并发模型

单个 task 的命令清单（首参皆 `cmd`）：

```cpp
vkCmdBindPipeline(cmd, GRAPHICS, pipeline)
vkCmdSetViewport / vkCmdSetScissor(cmd, ...)
vkCmdBindVertexBuffers / vkCmdBindIndexBuffer(cmd, ...)
vkCmdBindDescriptorSets(cmd, ...)
vkCmdPushConstants(cmd, ...)
vkCmdDraw / vkCmdDrawIndexed(cmd, ...)
```

这里的 `cmd` 当前是 primary（模型 A）；模型 B 落地后会变成 task 自己的 secondary。A/B 边界由 §4 决定。

第三方渲染后端入口（例如 ImGui Vulkan backend）可以由 renderable 直接调用，只要它同样只把 draw 命令录进这个 `cmd`，并且不接管 `vkCmdBeginRendering` / `vkCmdEndRendering` / submit / present。项目不为这类三方库再包一层。

### 3.4 结束一帧（`end_rendering` + `end_frame`）

```cpp
end_rendering():
    vkCmdEndRendering(primary_cmd)

end_frame():
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
```

要点：
- **submit 是每帧的 join 点**：当前模型 A 下本帧所有 task 已按 FIFO 录进 primary command buffer 后才能提交；模型 B 落地后则是在 secondaries 已 `vkCmdExecuteCommands` 后提交。
- `wait` 阶段设 `COLOR_ATTACHMENT_OUTPUT`，让顶点等早期阶段与 acquire 重叠。
- `render_finished` 严格应**每 swapchain image 一个**（避免呈现期被复用）——小项目易忽略，记一笔。
- **resize / OUT_OF_DATE**：`wait_idle` → 重建 swapchain / views / depth。

### 3.5 结束全部（关机）

次序为硬约束（GPU 在用的不能毁、device 不能先于其资源毁）：

```cpp
1. running=false → 排空 render scope（确保无 task 在录 / 在飞）
2. vkDeviceWaitIdle(device)
3. 各 renderable 析构 → 毁自建的 pipeline / buffer / image / descriptor pool / sampler / shader module（未来模型 B：含 secondary 池）
4. renderer 毁自有 per-frame / swapchain 资源：depth_image（view → image → memory）、command_pool ×N、image_available / render_finished / in_flight、swapchain_image_views[] → swapchain；这些都在 device idle 之后，内部只需满足对象自身依赖。
5. device → surface → debug_messenger → instance
```

**第 1→3 步次序对协程架构是硬约束**：render scope 先排空、renderable 先析构，renderer 才能拆，否则 renderable 持有的 handle 在 device 销毁后变野指针（即先排空全部渲染任务、再 device wait idle、最后拆 renderer）。

---

## 4. 并发模型：A 串行 primary vs B 并行 secondary

「并发提供给多个绘制任务」在 Vulkan 里只有两种合法实现，由 §3 铁律逼出。

**并发事实速查**：

| 操作                                              | 能否并发                                      |
| ----------------------------------------------- | ----------------------------------------- |
| `vkCreate*`（pipeline / buffer / image / shader） | **能**（device 多为内部同步）→ renderable 初始化资源可并行 |
| 录命令进**同一** CB                                   | **不能** → 要么串行（A），要么各录各的 secondary（B）      |
| `vkQueueSubmit` / `vkQueuePresentKHR`           | **不能**（queue 外部同步）→ 提交点串行                 |

> 所以最划算的并行在**资源创建**与**录制前的 CPU 计算**（动画 resolve / 剔除 / 矩阵 / 上传）；录制本身要么串行，要么走 secondary。

### 4.1 模型 A：单 primary CB，串行录制 —— 基线，**已走通**

所有 task 录进同一个 primary command buffer；scheduler 串行、按先后依次驱动：

```cpp
begin_rendering(frame)
for (task : 按既定先后排好的任务)     // 串行，FIFO
    task 录命令 into primary_cmd      // 录制顺序 == 绘制顺序（先后）
end_rendering()
```

- 「并发」仅在 CPU 侧*录制前*的活；`vkCmd*` 进 CB 那刻必须串行。
- 串行 FIFO 执行器按需要的先后依次驱动，录制顺序即绘制顺序。
- 优点：零 secondary 开销、最简。缺点：录制不能并行。
- **现状：A 已走通（M0 / M1 当前实现），作为当前契约与 B 的基线保留。**

### 4.2 模型 B：secondary command buffer，并行录制 + 控序 —— 演进方向

每个 render task 自带 `VkCommandPool` + secondary CB（池亦外部同步，故**每并行录制单元 / 每 task 一套**），并行录制后由 renderer 按序执行：

```cpp
// task（可并行）
vkBeginCommandBuffer(sec, {
    flags       = RENDER_PASS_CONTINUE_BIT,
    inheritance = VkCommandBufferInheritanceRenderingInfo{ color/depth 格式, rasterizationSamples }   // dynamic rendering 必填
})
录 draw into sec        // ← 这些 sec 可并行录
vkEndCommandBuffer(sec)
把 sec 交回 renderer / scheduler

// renderer（串行 join）
begin_rendering(frame, contents = SECONDARY_COMMAND_BUFFERS_BIT)
按需要的先后排列 secondaries（现阶段 = 提交顺序）
vkCmdExecuteCommands(primary_cmd, n, secondaries[])   // 数组顺序 == 执行顺序 == 绘制先后
end_rendering()
```

**为什么把 B 作为演进方向（同时满足全部既有取舍）**：
- 并发录制 ✅。
- 控序 ✅：`vkCmdExecuteCommands` 数组顺序即绘制先后——**用排列而非抽象控制先后**（排序策略本身待定，§5）。
- renderable 仍裸 `vkCmd` ✅：它只是录进自己的 secondary，不感知 primary。
- 不引入 draw_item 第二套 API ✅（呼应 §2.4「不做第二套 Vulkan API」）。
- 代价：secondary 管理 + 每并行录制单元一池 + 少量驱动开销；secondary 须**备 frames-in-flight 份**并随帧 reset。

**归位**：§2.2 所说 renderable「录进自己的 command buffer」，在模型 B 下即每帧的 **secondary**——录完交回 renderer，由 `vkCmdExecuteCommands` 按既定先后执行。

---

## 5. render scheduler 契约（当前模型 A）

供 scheduler 实现遵循：

1. **每帧 FIFO 流水**：`begin_frame → begin_rendering(PRIMARY) →（按注册顺序串行驱动所有 task 录进 primary）→ end_rendering → submit → present`。submit 前必须让本帧全部 content task 跑完。（这套帧开闭在运行时由 render scheduler 的 `submit()` 驱动，见 `render-runtime.md §5`。）
2. **顺序归属**：当前模型 A 下绘制先后 = task 被 FIFO resume 后录进 primary command buffer 的顺序。**现阶段不排序**（按注册 / 提交顺序），排序键见待定。
3. **begin / end_rendering 归 renderer**；task 只认 `cmd`（当前 primary，未来 B 为 secondary），只录 draw，不碰 submit / present。
4. **frames-in-flight**：每帧槽独立 primary CB + sync；CPU 最多领先 GPU N 帧；CPU 每帧写的 GPU buffer 备 N 份（push constant 录进 CB 内，不在此列）。未来模型 B 下，每 task 的 secondary 池亦须按帧槽拆分。
5. **生命周期**：渲染收尾须先排空 render scope、再析构 renderable、最后拆 renderer（§3.5）。未来模型 B 下还包括 renderable 自持的 secondary 池。
6. **A↔B 与未来 CUDA**：task 只认 `cmd`，故 A↔B 切换、乃至换 CUDA renderer，renderable 无须改动（呼应 engine-spec §4.5「后端可换」）。

**已定的演进方向**：单 entity 可注册**多个 render task**（未来模型 B 下对应多个 secondary；粒度落在 render task 而非 entity）——这样跨深度的情形（如 A 的剑要盖在更近的 B 前）也能表达。

**待定（动手时敲）**：
- **排序键（暂不做）**：游戏现阶段无需处理绘制排序，先按提交顺序。将来需要时再引入排序键，并定其取值与传输（task 返回值带出 vs 调度前由快照统一算好分配，倾向后者）。
- task↔secondary 池的**归属**：池由 renderable 自持，还是 scheduler 池化复用（避免每英雄一池的碎片）？
