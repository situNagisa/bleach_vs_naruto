# bvn 技术架构决策日志

> 日期：2026-07-01（起 2026-06-14）　状态：**决策日志·非权威**
> 本文记录每条决策的取舍与来龙去脉（想知道「为什么是这样」看这里）；规范正文见 [engine-spec.md](engine-spec.md#bvn-引擎架构定稿engine-spec-v1)。段末 `T#` 仅作架构访谈的溯源标签，不代表阅读顺序。
> 文档分工与阅读路径见 [context.md](context.md#bvn-文档索引context)。

---

## 1. 工程基建（T1）

- **C++ 标准：C++26**（最前沿；当前编译器仅部分支持，MSVC `/std:c++latest`、Clang `-std=c++2c`；一旦**静态反射**可用，对 ECS / 序列化 / 插件元数据是大杀器）。
- **依赖管理：vcpkg**。
- **工具链：clang-cl 优先，MSVC 兜底**（clang-cl 用 MSVC ABI；注意热重载工具偏好 MSVC，开发构建可用 MSVC）。
- **模块组织：单仓多静态库** + 英雄 DLL + client/server 可执行。

---

## 2. 范式与并发（T2）

- **ECS：EnTT**（纪律：① 迭代顺序稳定；② 上 CUDA 时从 packed 组件数组抽连续数据）。
- **错误处理**：可恢复的运行时错误：用**异常**；程序 Bug / 逻辑错误：用**断言 / 契约（C++26）**；不可恢复错误：炸程序。（落地例子见 [coding-standard.md §需要展开的几点](coding-standard.md#需要展开的几点) 的错误处理。）
- **并发：`std::execution`（senders/receivers，先用 NVIDIA stdexec）+ 协程**，**线程作 scheduler 资源**——调度器对外只承诺串行 / 并行，线程是它内部分配的资源。

---

## 3. 仿真与确定性（T3）

- **tick：可配置，先 30Hz**。
- **确定性：不强求，但守便宜的纪律留门**——随机走 simulator 持有的种子 RNG（禁 `rand()` / 全局）、sim 内禁墙钟（时间 = tick）、EnTT 稳定迭代、集中封装"游戏数值运算"。将来上 lockstep 时再约束并行 + 换确定数值。
- **simulator→render：双缓冲快照 + 插值**（配合 `execution`：simulator 写后缓冲、渲染读前缓冲，零锁）。

---

## 4. 渲染（T6）

> 规范细节见 render/ 各文档（[renderer.md](render/renderer.md#bvn-渲染renderer设计-规范) / [renderable.md](render/renderable.md#bvn-渲染renderable设计-规范) / [render-task.md](render/render-task.md#bvn-渲染render-task设计-规范) / [render-scheduler.md](render/render-scheduler.md#bvn-渲染render-scheduler设计-规范) 等）。本节只记决策取舍。

- **renderer**：先不考虑怎么抽象，直接先用 vulkan 的原生 API，但清楚 vulkan 是作为 renderer 层来用的，使用 vulkan 的地方不能超过 renderer 的界定范围（利将来 CUDA interop；不上 bindless / render-graph）。
- **渲染读 simulator：双缓冲快照 + 插值**（T3 定）；渲染侧每帧从快照提取 render scene。
- **2D 动画 = 招式帧数据时间线 + 走位状态机**：招式写成共享"帧数据时间线"（startup/active/recovery + 逐帧 hit/hurt box + 取消窗口 + 关联 clip），**simulator 与表现同源读取**（天然同步；simulator 只跑极轻的时间线推进器，完整动画状态机不进 simulator）；走位 / 受击 = 表现侧状态机，由参数驱动。M1–M2 用 C++ / struct 定义，M3 升 Lua / 数据 authored。**角色开发者 ~90% 填数据、~10% 写具名 C++ 钩子。**
- **光照：3D 场景上完整动态光 + 法线 + 阴影；2D 精灵 unlit**（平面精灵无法线、BvN 自带画风，不给精灵打动态光）。

---

## 5. 内容管线与数据（T5）

- **脚本 + 可脚本化数据语言：Lua**（引擎内）。理由：为嵌入而生、无 GIL（多 `lua_State` 天然并行，契合并发 execution）、LuaJIT 热循环接近原生、GC 可控。**Python 只放引擎外工具链**（GIL 与并发冲突、运行时重）。
- **数据 = Lua 表**（可脚本化数据；约定"能声明式就声明式"，便于将来编辑器 / 校验）。
- **Lua 时机：语言现在定、深度（配置-only vs 纯 Lua 内容）推迟到 M3**。M0–M2 零 Lua（Live++ 的 C++ 热重载已覆盖大半迭代需求；Lua 核心价值收敛为"低门槛 modding 路径 + 可脚本化数据"）。
- **资源：运行时直接加载 + 热重载**（直吃 BvN 精灵表 + 元数据）。

---

## 6. 插件与热重载（T4a / T4b）

> 前提：**明确不考虑跨编译器、安全后置** → 目标 = 插件最大自由度 + 最高性能。

**边界与结构（T4a）**

- **ABI 边界：C++ 虚接口**（共享头文件、同工具链编译）。虚调用开销只在"每 tick 一次"的粗粒度边界，可忽略；内层热循环插件直接碰 ECS、零虚调用。
- **宿主 API：直接暴露 ECS / 引擎内部**（插件 = 住在 DLL 里、和引擎紧耦合的一等 C++ 模块）。
- **实例状态：插件持有富 C++ 对象**。
- **热重载：路 0（数据 / Lua 热重载）+ 路 3（实时打补丁工具 blink → Live++）**；不走 POD 妥协。

**范围与策略（T4b）**

- **范围：万物皆插件**（英雄 + 装备 + 地图 + 模式）。共用一套插件基建（加载 / manifest / 版本 / 热重载 / 宿主访问），各自接口不同。**分阶段实现**：M2 英雄 API → M3 装备 → 之后地图 / 模式。
- **粒度：每英雄一个 DLL**（独立热重载 / 独立分发）。
- **加载：扫描 `plugins/` 目录 + 每插件 manifest**（id / 版本 / 依赖 / 资源路径）。
- **版本化：极轻量**——manifest 写引擎 ABI 整数版本，加载校验、不匹配拒载 + 日志；开放第三方 mod 时再升级到语义版本 + 依赖解析。

---

## 7. 网络（T7 / T8）

> 思路正文见 [net.md §思路](net.md#思路)。本节只记决策取舍。

- **T7 网络**：`net_transport`(ENet) · `input_command{moveDir,aim,abilityBits,seq,tick}` · **host 权威 + 快照 + 移动预测 + 和解** · 快照演进 全量→delta→AOI（兼迷雾地基）· 可演进到专用无头服务器。

---

## 8. 工具与可观测（T9）

> 编辑器决策已定（2026-06-15）。

- **英雄编辑器（Fighter Factory 式，重头戏）**：**引擎内 ImGui 工具**，复用引擎渲染 + 动画系统做**所见即所得实时预览 + 热重载**（外部工具给不了的杀手锏）。功能蓝图对标 MUGEN：精灵 / clip、**逐帧 hit/hurt box（CLSN1/CLSN2）**、帧数据时间线（startup/active/recovery）、取消 / 连段窗口、状态机图、动画事件、键位路由。
  - **判定模型升级**：把"判定 radius"升级为**逐帧 hit/hurt box**（有编辑器画框才可行——真正的格斗手感来源）。
  - **数据格式要求**：声明式数据必须**干净、可被编辑器往返（load/save/round-trip）**——即 MUGEN"数据 + 具名控制器"。
  - **美术桥**：Animate CC（BvN 即 Flash）→ **导出 sprite sheet** → 编辑器在其上定义玩法数据。先光栅（矢量运行时如 Rive 是另一个大坑，自研光栅更合适）。
  - **三阶段分工**：① Animate CC 做美术帧 → ② 你的编辑器做玩法数据 → ③ 引擎运行时加载。
  - **时序**：M2–M3 先手搓 1–2 英雄稳定数据格式 → M3–M4 再建编辑器量产。
- **其余 T9（待细化）**：ImGui 调试覆盖层、日志、profiling、replay / 录像。

---

## 9. 横切工程原则

> 不属于某个单一领域、贯穿全项目的设计规则集中放这里（以后还会长）。

- **资源所有权优先于版本对比**：当子对象依赖父对象的资源时，优先把该资源的**所有权移交给子对象**、让子对象各自独立持有；**不要**擅自在子对象里加"对比父对象版本号 → 重建"的逻辑——那是额外复杂度 + 隐式耦合。拿不准所有权归属时先讨论，别默默加版本字段。
- **RAII 生命周期 · 瞬态资源进协程帧**：有生命周期的对象（如 GPU 句柄）用 move-only 的 RAII 拥有者管理（构造即获取、析构即释放），避免在调用点逐个判空 teardown；瞬态 GPU 资源作为**协程帧内的局部量**持有（详见 [render/render-task.md §1 render task 的形态](render/render-task.md#1-render-task-的形态)；编码落地见 [coding-standard.md §需要展开的几点](coding-standard.md#需要展开的几点) 的错误处理）。
