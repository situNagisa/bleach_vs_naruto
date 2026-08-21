# 任务 04：交叉动画系统（sml 状态机 · 可复用辅助件）+ hero 接入

> 目标：设计并落地一个**可复用**的 2D 精灵动画辅助库：clip 时间线 + **交叉过渡（crossfade：旧 clip 淡出、新 clip 淡入）** + 用 [boost-ext/sml](https://github.com/boost-ext/sml) 写的表现侧状态机；hero 作为首个消费者接入（替换现有硬编码的 idle/walk/run 速度阈值切换，切换处产生交叉淡化）。
> 依据：[../animation.md](../animation.md) 的既有定调**必须全部满足**——动画是消费者侧可选辅助件、极简、可序列化；权威动画态按 tick 在 sim 推进、写 ECS/快照；**推进 = 纯函数 (state, dt_tick) → state**；单帧 ⟂ 时间轴（renderable.md §4）。
> 依赖：任务 01（hero 已在 concept 边界上）。与 02/03 无耦合，可并行。

---

## 1. 依赖引入：sml

- header-only 单头文件。优先走 vcpkg：`vcpkg search sml` 确认端口名（大概率是 `sml`；若叫 `bext-sml` 就用那个），加进 `vcpkg.json` dependencies。
- 若 vcpkg 拉不到（离线/无端口）：vendor 单头进 `include/third_party/sml/sml.hpp`（MIT，三方库直接用，不包一层），vcxproj 加 ClInclude。
- MSVC 兼容性注意：sml 在 MSVC 下须 `/permissive-`（已开）；若遇到编译问题优先升 sml 版本（v1.1.11+）。

## 2. 库设计（新目录 `include/bvn/animation/`，全 header-only）

### 2.1 分层（单帧 ⟂ 时间轴的库化）

```
clip.h        —— clip 描述：帧数、每帧 tick 数（或逐帧 tick 表）、是否循环。纯数据。
playback.h    —— 播放态 + 采样：POD 播放态 { clip 索引, 起始 tick }；
                 纯函数 sample(clip, playback, now_tick) -> 帧号（循环/夹取）。
crossfade.h   —— 交叉过渡：POD { from_clip, from_playback, fade_start_tick, fade_ticks }；
                 纯函数 resolve(clips, anim_state, now_tick) -> single_frame
                 single_frame = { from: {clip, frame, weight}?, to: {clip, frame, weight} }
                 权重线性 ramp：fade 窗口内 from 1→0、to 0→1；窗口外只有 to（weight=1）。
machine.h     —— sml 集成薄壳：见 §2.2。
```

- **耐久动画态是一个 POD 聚合**（可直接进 ECS 组件 / preview_state / 快照）：

```cpp
struct animation_state        // 全 POD，可 memcpy、可序列化（服务 dump/快照/热重载）
{
	::std::uint32_t clip = 0;                 // 当前 clip
	::std::uint64_t clip_start_tick = 0;      // 当前 clip 起始 tick
	::std::uint32_t fade_from_clip = 0;       // 交叉来源（fade_ticks==0 表示无过渡）
	::std::uint64_t fade_from_start_tick = 0;
	::std::uint64_t fade_start_tick = 0;
	::std::uint32_t fade_ticks = 0;
};
```

- **切换入口是纯函数**：`switch_clip(state, new_clip, now_tick, fade_ticks) -> animation_state`（把当前 clip 记为 fade 来源、开新 clip；若仍在上一段 fade 中，简化为丢弃更早的来源、只保留最近一层——极简原则，不做多层混合）。
- 库**不含** vulkan / renderer 依赖——它只算"这一帧画什么、各多少权重"；怎么画（两张 quad、alpha）是消费者的事。

### 2.2 sml 的角色与状态二分

- **状态机是瞬态**（表现侧控制流）→ 按状态二分放**协程局部/成员**，不进快照；**进快照的只有 `animation_state` POD**。
- `machine.h` 提供的薄壳只做两件事（不试图通用化 sml）：
  1. 约定：消费者写自己的 sml 转移表（状态 = 动作，事件自定义），**转移的 action 里调 `switch_clip`** 更新 POD（sml 依赖注入把 `animation_state&`、`now_tick`、fade 时长传进 action）；
  2. 重建：提供"从耐久态恢复机器"的模式说明——sml 不能任意跳状态，消费者转移表须包含 `restore` 事件（`state_x + event<restore> [guard: target==x] = state_x` 每状态一条，或初始态分发），薄壳在构造后 dispatch 一次 `restore{state.clip}`。写清即可，实现留给消费者表。
- 这样"动画状态机可序列化"的真正含义 = POD 可序列化 + 机器可由 POD 重建，与 animation.md"只为可序列化"的定调一致。

### 2.3 与 sim 的关系（本阶段的务实简化）

animation.md §2 要求权威动画态在 sim 按 tick 推进。当前 client 的 sim 是 `preview_simulation`（位置/速度），动画态放在 `preview_state`（registry.ctx）里由 preview_controller 每个 sim step 后推进：

- preview_controller 的 sim step 循环里，用速度算事件（阈值与现状一致：0.5 走 / 5.0 跑），喂给 hero 的状态机（机器与 POD 都挂在 preview_state 里或 controller 成员 + POD 在 preview_state——**POD 必须在 preview_state**，渲染只读它）。
- hero.render 每帧只读快照里的 `animation_state` + tick → `resolve` → 画。**渲染侧不再有 action 判定逻辑**（现有 speed→action 的 if 链删除）。

## 3. hero 接入（消费者示范）

1. 三个 clip：idle/walk/run（现有 gif 资源）。clip 描述从 `sprite_clip_data`（帧数）+ 现有 `animation_ticks_per_frame = 4` 组装。
2. sml 表（示意）：

```
idle + speed_changed [v >= run_th]  / switch(run,  fade=5) = run
idle + speed_changed [v >= walk_th] / switch(walk, fade=4) = walk
walk + speed_changed [v <  walk_th] / switch(idle, fade=4) = idle
walk + speed_changed [v >= run_th]  / switch(run,  fade=4) = run
run  + ...（对称）
*    + restore{c} ...（重建）
```

   fade 取 4–5 个 sim tick（约 130–170ms），手感再调。
3. 渲染：`resolve` 给出至多两个采样。fade 窗口内**画两张 quad**：先旧后新，各自的 uv_rect 按各自 clip/帧算，透明度 = 权重。
   - **shader 改动**：`push_constants` 加一个 `float alpha`（注意 16 字节对齐，vec4 uv_rect 后追加 float + padding，或并进一个 vec4）；`shaders/sprite.frag` 输出颜色乘 alpha（预乘混合已启用：srcAlpha/one-minus-srcAlpha，直接乘进 a 与 rgb 一致性自查）。
   - `shaders/` 下找 `.vert/.frag` 源码与现有 spv 的编译方式（glslang 已在 vcpkg feature tools；查 shaders/ 里是否有编译脚本或看 git 历史怎么生成的 spv），重编 `sprite.*.spv`。
   - 两张 quad 同 pipeline、同深度状态（精灵管线深度测试本就关闭），仅 push constants 与 descriptor set 不同 → 录制两次 draw。
4. `hero_action` 枚举与 `sprite_clip_frame.action` 等旧路径清理：动作判定逻辑集中到状态机事件；渲染读 `animation_state`。

## 4. 落地后要更新的正式文档

- [../animation.md](../animation.md)：
  - §1 定位保留（消费者侧可选辅助件），删"当前暂缓/彻底不做"（doc-spec §3.2）；
  - 新增一节"交叉过渡（第一版辅助件）"：clip/播放态/交叉 POD + 推进纯函数 + 状态机瞬态、POD 耐久的二分 + sml 重建模式（`restore` 事件），高信息密度、一屏内讲完；
  - §4 计划改为：已落地第一版（时间线 + 交叉），帧数据 authored（Lua）与 hit/hurt 框仍按原计划后置。
- [../context.md](../context.md) animation 行的"何时读/讲什么"随内容微调（一句话）。
- 若判断内容已够厚可拆 `docs/animation/`（design/impl 分文件）——**不强制**，单文件塞得下就别拆（doc-spec §3.3 别凑结构）。

## 5. 验收

- 走↔跑↔停切换时肉眼可见交叉淡化（一瞬间两张精灵叠加、权重互补），无闪黑/跳帧。
- 动画推进只依赖 tick（暂停 sim 时画面帧不再前进；恢复后连续）。
- `animation_state` 是 trivially copyable（`static_assert(::std::is_trivially_copyable_v<...>)` 写进库头）。
- 库头文件不含 vulkan include。
- commit 建议拆两笔：`animation 辅助库：clip/播放/交叉纯函数 + sml 集成约定`、`hero 接入交叉动画（sml 状态机 + 双 quad 淡化）+ shader alpha + 文档同步`。
