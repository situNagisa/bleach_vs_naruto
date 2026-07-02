# bvn 渲染·并发模型 A / B（设计）

> 「把并发提供给多个绘制任务」在 vulkan 里只有两种合法实现，由帧生命周期的铁律逼出。本文讲两个模型的约束与设计取舍。
> 铁律与帧生命周期见 ../renderer.md §2；render scheduler 如何据此对外承诺串行见 ../render-scheduler.md；A 已走通的现状与妥协见 ./impl.md。

---

## 1. 并发事实速查

| 操作                                              | 能否并发                                      |
| ----------------------------------------------- | ----------------------------------------- |
| `vkCreate*`（pipeline / buffer / image / shader） | **能**（device 多为内部同步）→ renderable 初始化资源可并行 |
| 录命令进**同一** CB                                   | **不能** → 要么串行（A），要么各录各的 secondary（B）      |
| `vkQueueSubmit` / `vkQueuePresentKHR`           | **不能**（queue 外部同步）→ 提交点串行                 |

> 所以最划算的并行在**资源创建**与**录制前的 CPU 计算**（动画 resolve / 剔除 / 矩阵 / 上传）；录制本身要么串行，要么走 secondary。

---

## 2. 模型 A：单 primary CB，串行录制 —— 基线

所有 task 录进同一个 primary command buffer；scheduler 串行、按先后依次驱动：

```cpp
begin_rendering(frame)               // submit() 的开台括号
for (task : 按既定先后排好的任务)     // 串行，FIFO
    task 录命令 into primary_cmd      // 录制顺序 == 绘制顺序（先后）
end_rendering()                      // submit() 的收台括号
```

- 「并发」仅在 CPU 侧*录制前*的活；`vkCmd*` 进 CB 那刻必须串行。
- 串行 FIFO 执行器按需要的先后依次驱动，录制顺序即绘制顺序。
- 优点：零 secondary 开销、最简。缺点：录制不能并行。

---

## 3. 模型 B：secondary command buffer，并行录制 + 控序 —— 演进方向

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
按需要的先后排列 secondaries（现阶段 = 提交顺序）
vkCmdExecuteCommands(primary_cmd, n, secondaries[])   // 数组顺序 == 执行顺序 == 绘制先后
end_rendering()
```

**为什么把 B 作为演进方向（同时满足全部既有取舍）**：
- 并发录制 ✅。
- 控序 ✅：`vkCmdExecuteCommands` 数组顺序即绘制先后——**用排列而非抽象控制先后**（排序策略本身待定，见 ./impl.md）。
- renderable 仍裸 `vkCmd` ✅：它只是录进自己的 secondary，不感知 primary。
- 不引入 draw_item 第二套 API ✅（呼应 ../renderer.md「不做第二套 vulkan API」）。
- 代价：secondary 管理 + 每并行录制单元一池 + 少量驱动开销；secondary 须**备 frames-in-flight 份**并随帧 reset。

> B 的完整示例代码见 ../vulkan-qa.md §5。

**归位**：renderable「录进自己的 command buffer」，在模型 B 下即每帧的 **secondary**——录完交回 `submit()`，由 `vkCmdExecuteCommands` 按既定先后执行。

---

## 4. 对 render task 透明

task 只认交给它的 `cmd`（A=primary / B=secondary），故 A↔B 切换、乃至将来换 CUDA renderer，render task 无须改动（呼应 renderable「后端可换」）。这条是把 A/B 差异关进 render scheduler / renderer、不外泄到内容侧的关键。
