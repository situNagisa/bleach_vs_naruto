# 任务 03：render context dump（旧注册表方案废弃）

> 本文原本依赖 task record / seq 注册表来 dump 和恢复绘制次序。当前 graphics 主线已经删除 task index 与注册表：render task 自己持有 secondary command pool / command buffer，每帧通过 `on_frame(pool, buffer)` awaitable 进入本帧录制。因此旧方案不再作为实现依据。

---

## 1. 仍然成立的部分

按状态二分（engine-spec §6），render task 的依赖仍拆成三层：

| 层 | 现在住哪 | dump 需要做什么 |
|---|---|---|
| 瞬态 GPU 资源（pipeline / buffer / texture / task secondary pool） | 协程帧局部量（RAII） | **不 dump**——重启协程自动重建 |
| 耐久游戏态（位置 / 动作 / tick / 相机） | ECS：`registry.ctx()` 的 preview_state 等 | **已经天然可保留**——重启协程读回即可 |
| 绘制次序 | 当前由 render task 启动 / waiter 停靠顺序决定 | 若需要跨热重载强恢复，必须另设显式排序键 |

---

## 2. 废弃的旧方案

不要再按下面方向实现：

- task registry / seq 表；
- task 下标；
- 按 id 注册 render task；
- retire / restore 注册记录；
- 由 `vulkan_context` 持有 per-task secondary 资源。

这些都会把当前已经简化掉的 workflow state 重新引回来。

---

## 3. 后续如果真要做

如果热重载或 dump/restore 确实要求“跨协程重启后层级绝对不变”，新增任务应只解决排序问题：

- render task 仍然只接 global env；
- frame env 仍然由 `on_frame(pool, buffer)` awaitable 返回；
- secondary command pool / command buffer 仍然由 task 持有；
- workflow 可以接受一个显式排序键或由宿主按固定顺序重启 task，但不能恢复旧 task index 注册表。

验收也应围绕“重启后 waiter / 排序键顺序稳定”来写，而不是检查注册表 dump。
