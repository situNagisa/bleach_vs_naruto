# bvn 渲染·并发模型 A / B（设计）

> Vulkan command recording 的两种并发模型及取舍；主线采用模型 B。
> 帧生命周期见 [../renderer.md §2](../renderer.md#2-帧生命周期五阶段规范级)；render scheduler 契约见 [../render-scheduler.md](../render-scheduler.md#bvn-渲染render-scheduler设计-规范)。

---

## 1. 并发事实

| 操作 | 并发约束 |
|---|---|
| `vkCreate*`（pipeline / buffer / image / shader） | device 通常内部同步，renderable 初始化资源可并行 |
| 向同一个 command buffer 录命令 | 需要外部同步；模型 A 串行，模型 B 各录各的 secondary |
| 同一个 `VkCommandPool` 的 allocate / reset 等操作 | 需要外部同步 |
| `vkQueueSubmit*` / `vkQueuePresentKHR` | queue 需要外部同步，提交点串行 |

模型 A 用串行满足同一 command buffer 的同步要求；模型 B 为每个并行 render task 提供独立 pool，并行录制 secondary，最后串行 join。

---

## 2. 模型 A：单 primary command buffer，串行录制

```cpp
begin_rendering(frame);
for (auto&& task : tasks)
{
	task.record(primary_command_buffer);
}
end_rendering(frame);
```

| 特性 | 结果 |
|---|---|
| command buffer | 所有 task 共用 primary |
| 录制调度 | 串行 |
| secondary 开销 | 无 |
| 绘制顺序 | 与 task 驱动顺序一致 |

模型 A 适合作为串行退化路径。

---

## 3. 模型 B：secondary command buffer，并行录制

每个 render task 持有一个 `secondary_command_pool`；pool 内 command buffer 数量按实际在途 generation 动态增长，并通过 free list 复用。task 每次只处理 `async_record(pool)` 返回的一个 generation。

模型 B 的数据流为：`acquire/reset + 预留未发布条目 → 录制 secondary → retirement operation 原子发布 → workflow 收集已发布条目 → primary join`。完整 task 代码只在 [../render-task.md §1](../render-task.md#1-render-task-的形态) 维护。

### 3.1 资源与同步边界

| 边界 | 规则 |
|---|---|
| task pool | 每 task 一个；该 task 负责 Vulkan pool 与全部 secondary buffer 的生命周期 |
| task 看到的 frame | 每轮一个 `render_recording_frame`；不包含 frame slot 数量 |
| generation secondary 集合 | workflow 持有，一个 mutex 保护预留、发布与收集 |
| 发布事务 | 录制成功且 `stdexec::spawn` 启动 retirement operation 后才发布；未发布条目不执行 |
| command 回收 | retirement sender 等精确 generation 完成后归还 task free list |
| generation 完成 | queue submit 前失败时立即完成；成功提交后等 slot fence；shutdown 在 GPU idle 后完成全部在途 generation |
| join 顺序 | 按并发预留形成，绘制顺序未定义 |

同一 task 的 coroutine 每次只在一个线程上同步录制；retirement callback 只在 mutex 下更新 free list，不调用 `vkFreeCommandBuffers`。task close / join 全部 retirement 后，buffer owner 先析构，Vulkan pool 后析构。

### 3.2 取舍

| 收益 | 代价 |
|---|---|
| 不同 task 可并行录制 | 需要 secondary inheritance 与 primary join |
| task 不维护 frame slot 数量 | 在途 generation 增多时动态分配更多 command buffer |
| task 数量可动态变化，销毁时只处理自己的 pool | 每 task 一个 Vulkan command pool |
| 一个 task 每帧可产生零个或多个 command buffer | 每个 command 需要一个 retirement association |

模型 B 保持 renderable 直接使用 Vulkan `vkCmd*`，同时把 frame slot、fence 与回收时机集中在 render workflow。完整 task 形态见 [../render-task.md §1](../render-task.md#1-render-task-的形态)。
