# 任务 06：热重载（DLL 换新 → 取消旧协程 → 从 ECS 态重启）

> 目标：兑现 engine-spec §6"热重载 = 重启协程"：运行中检测插件 DLL 被重新编译 → 停掉该插件全部协程 → 卸旧载新 → **从 ECS 当前态重启**新协程（瞬时态丢失可接受）。绘制次序恢复需要按当前无 task index 的 render workflow 重新设计，不能沿用旧注册表方案。
> 定位：这是 [../plugin/hot-reload.md](../plugin/hot-reload.md) 里"路 0/路 3"之外的 **DLL 级重载**，为 Live++（M3 路 3）之前提供可用的全链路；数据/资源热重载已有（resource_cache.poll，hero 贴图已在用）。
> 依赖：任务 03（按 id 复用绘制次序）、任务 05（插件宿主、per-plugin scope、shadow copy 加载）。

---

## 1. 机制总览

```
每帧 poll ─→ DLL 文件身份变化？──否──→ 继续
                 │是
                 ▼
        阶段 1  plugin.scope.request_stop()
                 │   主循环照常跑帧（关键：等待中的 render task 要靠 submit() 唤醒才能观察到 stop）
                 ▼
        阶段 2  等 scope 排空（异步：spawn scope.on_empty() | then(置 atomic flag)，主循环每帧查 flag）
                 │   期间协程逐个醒来→见 stop→退出循环→协程帧析构释放瞬态 GPU 资源
                 ▼
        阶段 3  swap：先 shadow copy + 加载**新** DLL、解析入口、ABI 校验
                 │        成功 → 卸载旧 DLL（shadow 文件名不同，先新后旧无冲突）
                 │        失败 → 日志、保持无插件状态，文件再变化时重试（旧 DLL 已停，不回滚）
                 ▼
        阶段 4  重启：bvn_plugin_main(ctx) spawn 进新一轮 scope
                 │   耐久态从 registry 读回（状态二分的回报：test_basic 的 phase/tick 原地续）
                 │   render task 按宿主约定顺序重新启动；若未来需要强排序，另加显式排序键
```

## 2. 实现要点

1. **watch**：`loaded_plugin` 里记 DLL 原路径的文件身份（last_write_time + size，手法照抄 `resource_cache.h` 的 file_identity）。`plugin_host.poll()` 由 client 主循环每帧调用（与 `poll_events` 同处）。文件正被写入时 CopyFile 可能失败 → 视为"这次没变"，下帧重试（用 copy 成败做去抖，无须 sleep）。
2. **绝不能在主线程阻塞等排空**：主循环是 submit() 的驱动者，`sync_wait(scope.on_empty())` 可能死锁（等待中的 render task 永远等不到下一帧）。必须走 §1 的跨帧状态机（`enum class reload_stage { idle, stopping, swapping }` 挂在 loaded_plugin 上）。
3. **scope 复用**：`exec::async_scope` request_stop 后不可复位 → 阶段 4 用**新的 scope**（loaded_plugin 里放 `::std::unique_ptr<::exec::async_scope>` 或每轮重建 loaded_plugin 的运行时部分）。
4. **绘制次序恢复**：当前 `render_workflow` 没有 task index / 注册表 / retire 接口。第一版先要求宿主按确定顺序重启插件 render task；若热重载必须跨卸载保留严格层级，再单独引入显式排序键，不能复活旧的 render-task 注册方案。
5. **GPU 安全**：render task 退出路径本就 `vkDeviceWaitIdle` 后才析构 vk 局部量（现有 entity 模式，插件照抄）；per-task secondary 池归 render task 自己持有，scope 排空后随协程帧一起析构。插件侧只接 `global_dynamic_forward_env_renderer` / `frame_dynamic_forward_env_renderer` 值，不在宿主侧留下指向插件代码的函数指针或虚表；frame env 每帧由宿主 `on_frame(pool, buffer)` 构造并交给 task。落地热重载时把这条当验收点：卸载插件后继续跑帧不得崩。
6. **manifest 变化**（abi 改动等）同样走 reload 流程，阶段 3 重新校验。

## 3. 开发体验（验收剧本）

1. 起游戏，test_basic 方块在转，overlay 正常。
2. 不关游戏，改 `plugins/test_basic/test_plugin.cpp`（比如旋转速度 ×2 或换颜色），单独重编插件：
   `MSBuild plugins\test_basic\test_basic.vcxproj -p:Configuration=Debug -p:Platform=x64`
3. 1–2 秒内：方块消失一两帧（停机窗口）→ 重新出现，**转速变了但相位/tick 从中断处继续**（耐久态存续证明）、**层级按宿主启动顺序保持稳定**。
4. 日志完整打出：change detected → stopping → swapped(old→new shadow 名) → restarted。
5. 故障注入：改出编译错误 → 旧 DLL 保持运行不受影响（重编失败根本不产生新文件）；把 abi 改错 → 停机后拒载 + 日志，修回后自动恢复。

## 4. 落地后要更新的正式文档

- [../plugin/hot-reload.md](../plugin/hot-reload.md)：加"现状"短节——DLL 级重载（重启协程、耐久态走 ECS、shadow copy、绘制次序按宿主启动顺序恢复或未来排序键恢复）已落地；路 3（blink/Live++，函数级补丁不丢瞬态）仍是 M3 计划，二者互补不互斥。教学正文不动。
- [../plugin/impl.md](../plugin/impl.md)（任务 05 建的）：补 reload 状态机、render task 重启顺序约定、"插件协程必须由宿主 scope 管理"铁律。
- [../asset.md](../asset.md) 若有"热重载"表述与现状出入，核对一句（数据热重载 resource_cache 已在用）。

## 5. 验收

- §3 剧本全过；重复 reload 十次无泄漏式崩溃（shadow 旧文件可留待退出清理或启动时清 `.hot/`）。
- 关窗时若正处 stopping/swapping 阶段也能干净退出（收尾把 plugin scope 一并排空）。
- commit 建议：`插件热重载：文件监视 + 跨帧停机状态机 + 先载新后卸旧 + 从 ECS 态重启（绘制次序按 id 复用）+ 文档同步`。
