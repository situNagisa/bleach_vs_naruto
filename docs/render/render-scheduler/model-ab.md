# bvn 渲染·并发模型 A / B（设计）

> 「把并发提供给多个绘制任务」在 vulkan 里只有两种合法实现，由帧生命周期的铁律逼出。本文讲两个模型的约束与设计取舍，并说明**为什么设计奔着模型 B 走**。
> 铁律与帧生命周期见 [../renderer.md §2](../renderer.md#2-帧生命周期五阶段规范级)；render scheduler 据此对外承诺的顺序语义见 [../render-scheduler.md §1](../render-scheduler.md#1-两条-scheduler)；模型 A 作为当前基线的实现与妥协见 [./impl.md](./impl.md#bvn-渲染render-scheduler-当前实现与妥协现状-计划)。

---

## 1. 并发事实速查

| 操作                                              | 能否并发                                      |
| ----------------------------------------------- | ----------------------------------------- |
| `vkCreate*`（pipeline / buffer / image / shader） | **能**（device 多为内部同步）→ renderable 初始化资源可并行 |
| 录命令进**同一** CB                                   | **不能** → 要么串行（A），要么各录各的 secondary（B）      |
| `vkQueueSubmit` / `vkQueuePresentKHR`           | **不能**（queue 外部同步）→ 提交点串行                 |

> 所以最划算的并行在**资源创建**与**录制前的 CPU 计算**（动画 resolve / 剔除 / 矩阵 / 上传）；录制本身要么串行，要么走 secondary。

这张表逼出两个模型：**A** 用串行绕开"同一 CB 不能并发录"，**B** 用"各录各的 secondary"把录制也并行化。B 是设计目标（§3），A 是当前基线（§2）。

---

## 2. 模型 A：单 primary CB，串行录制 —— 当前基线

所有 task 录进同一个 primary command buffer；scheduler 串行、按注册序依次驱动：

```cpp
begin_rendering(frame)               // submit() 的开台括号
for (task : 按注册序排好的任务)        // 串行，FIFO
    task 录命令 into primary_cmd      // 录制顺序 == 绘制顺序
end_rendering()                      // submit() 的收台括号
```

- 「并发」仅在 CPU 侧*录制前*的活；`vkCmd*` 进 CB 那刻必须串行。
- 优点：零 secondary 开销、最简；是 render scheduler 顺序语义的一个退化特例（join 步骤为空）。
- 缺点：录制不能并行。
- **定位**：当前基线（M0/M1 走通），不是设计终态；现状与它为何先落地见 [./impl.md](./impl.md#bvn-渲染render-scheduler-当前实现与妥协现状-计划)。

---

## 3. 模型 B：secondary command buffer，并行录制 + 控序 —— 设计方向

每个 render task 自带 `VkCommandPool` + secondary CB（池亦外部同步，故**每并行录制单元 / 每 task 一套**），并行录制后由 render scheduler 的 `submit()` 按序执行：

```cpp
// task（可并行）
vkBeginCommandBuffer(sec, {
    flags       = RENDER_PASS_CONTINUE_BIT,
    inheritance = VkCommandBufferInheritanceRenderingInfo{ color/depth 格式, rasterizationSamples }   // dynamic rendering 必填
})
录 draw into sec        // ← 这些 sec 可并行录
vkEndCommandBuffer(sec)
把 sec 交回 render scheduler 的 `submit()`

// submit()（串行 join；render scheduler，非 renderer）
begin_rendering(frame, contents = SECONDARY_COMMAND_BUFFERS_BIT)
按注册序排列 secondaries
vkCmdExecuteCommands(primary_cmd, n, secondaries[])   // 数组顺序 == 执行顺序 == 绘制先后
end_rendering()
```

**为什么 B 是设计方向（同时满足全部既有取舍）**：
- 并发录制 ✅。
- 控序 ✅：`vkCmdExecuteCommands` 数组顺序即绘制先后——**用排列而非抽象控制先后**（排序策略本身待定，见 [./impl.md §4 待定](./impl.md#4-待定动手时敲)）。
- renderable 仍裸 `vkCmd` ✅：它只是录进自己的 secondary，不感知 primary。
- 不引入 draw_item 第二套 API ✅（呼应 [../renderer.md](../renderer.md#14-renderer-给-renderable-的接口边界)「不做第二套 vulkan API」）。
- 代价：secondary 管理 + 每并行录制单元一池 + 少量驱动开销；secondary 须**备 frames-in-flight 份**并随帧 reset。

> B 的完整示例代码见 [../vulkan-qa.md §5](../vulkan-qa.md#5-模型-b-示例代码示意)。

**归位**：renderable「录进自己的 command buffer」，在模型 B 下即每帧的 **secondary**——录完交回 `submit()`，由 `vkCmdExecuteCommands` 按注册序执行。

---

## 4. 对 render task 透明

task 只调 frame-env 的 `command_buffer()`（A=primary / B=自己的 secondary，由 frame-env 解析），故 A↔B 切换、乃至将来换 CUDA renderer，render task 无须改动（呼应 renderable「后端可换」）。这条是把 A/B 差异关进 render scheduler / renderer、不外泄到内容侧的关键（frame-env 二分见 [../renderer.md §1.0](../renderer.md#10-renderer-是一个-conceptvulkan-是它的一份实现)）。
