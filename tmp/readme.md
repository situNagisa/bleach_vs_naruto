# bvn 实现移交文档（docs/tmp）

> 本目录是**一次性移交文档**：把六个待实现任务的设计与实现思路写清，交给执行 agent（codex）落地。
> 不属于项目正式文档、不进 [../context.md](../context.md) 索引；全部任务落地并验收后**整目录删除**。
> 撰写日期：2026-07-07。撰写者已做完全部设计推演与文档核对，执行者照此实施即可；有出入时**以 docs/ 正式文档为准**（见下"权威依据"）。

---

## 1. 背景与现状

- 项目**文档驱动**：docs/ 先于代码。当前 `graphics` 主线已落地 renderer env 二分与模型 B；后续任务继续以 docs/ 正式文档和各 tmp 任务文档为准。
- 基线：分支 `vibe`，commit `4feb682`（"修复基线：清除 vulkan_renderer.cpp 中残留的 secondary_command_buffers 引用"）。**该基线可编译、可运行**（网格场地 + 精灵英雄 + ImGui overlay + 自由相机）。
- 工作区有一处未提交的文档改动：`docs/render/renderer-vulkan-impl.md` §7 访问器清单已按任务 01 的需要扩充（global-env 增 `graphics_queue_family()` 独立条目、`swapchain_image_count()`、`device_name()`；frame-env 增 `render_extent()`）。**保留它，随任务 01 一起提交。**

## 2. 构建与运行

- **构建入口是 `bvn.vcxproj`（MSBuild），不是 CMake**。CMakeLists 存在但非日常构建路径；改动以 vcxproj 为准，CMake 顺手同步即可（不强制）。
- MSBuild 位置（VS 2026，toolset v145）：
  `C:\Users\nagisa\program\visual_studio\2026\MSBuild\Current\Bin\MSBuild.exe`
- 构建命令（仓库根目录）：
  `MSBuild.exe bvn.vcxproj -p:Configuration=Debug -p:Platform=x64 -m`
- **新增源文件必须手工加进 `bvn.vcxproj`** 的 `ClInclude` / `ClCompile` ItemGroup（无通配）。解决方案文件是 `bvn.slnx`（新格式，VS 2026 支持）。
- vcpkg manifest 模式已启用（`VcpkgEnableManifest=true`），改 `vcpkg.json` 后 MSBuild 会自动装依赖。
- 已知无害警告：MSBuild 当前会报 `stdexec` / `nagisa` 头里的 C4324 / C4100；不是本项目 renderer split 产生的新错误。
- 运行：`bin\x64\Debug\bvn\bvn.exe`（依赖 `assets/source/*.gif` 与 `shaders/*.spv`，路径宏来自 `prop/solution.props`）。冒烟验证：窗口出现、能看到网格 + 精灵动画 + overlay、关窗干净退出（建议开着 validation layer 跑，`vulkan_renderer` 构造默认 `enable_validation = true`）。

## 3. 权威依据（凡事查文档）

- 索引：[../context.md](../context.md)。架构冲突 → [../engine-spec.md](../engine-spec.md)；C++ 风格 → [../coding-standard.md](../coding-standard.md)（Tab、Allman、snake_case、东 const、`::std` 全限定、struct 全公开、纯异常 + 断言、禁裸 new）；渲染 → docs/render/ 各文档；**改 docs 时守 [../doc-spec.md](docs/doc-spec.md)**（尤其 §3：不留已删实体、不写多余否定、不凑空话小节）。
- 每个任务文档里列了"落地后要更新的正式文档"清单——**代码合入的同一批提交里完成文档更新**，不要留下文档与代码不一致。

## 4. 任务清单与顺序

| # | 文档 | 内容 | 依赖 |
|---|---|---|---|
| 01 | [01-renderer-abstraction.md](01-renderer-abstraction.md) | renderer concept 二分 + vulkan 真身视图 + 动态转发壳，entity 迁移 | — |
| 02 | [02-render-scheduler-model-b.md](02-render-scheduler-model-b.md) | render scheduler 模型 B：task 自持 secondary + workflow 按 waiter 顺序 join | 01 |
| 03 | [03-render-context-dump.md](03-render-context-dump.md) | render context dump：旧注册表方案废弃，未来排序键另设 | 02 |
| 04 | [04-animation-crossfade-sml.md](04-animation-crossfade-sml.md) | 交叉动画系统（sml 状态机，可复用辅助件）+ hero 接入 | 01（与 02/03 无关，可并行） |
| 05 | [05-plugin-system.md](05-plugin-system.md) | 插件系统：扫描 + manifest + ABI 校验 + DLL 加载 + 工厂导出 + test_basic 插件 | 01、02（要用提升后的 render workflow 宿主接口） |
| 06 | [06-hot-reload.md](06-hot-reload.md) | 热重载：DLL 换新 → 取消旧协程 → 从 ECS 态重启 | 03、05 |

## 5. 版本管理（用户明确要求）

- **每个任务至少一个独立 commit**（大任务可拆多个小 commit），落在 `vibe` 分支，保证每个 commit 都能编译 + 冒烟通过，方便回溯。
- commit message 用中文，首行一句话概括，风格参照 `git log` 现有历史。
- 不要把无关的未跟踪内容（`.obsidian/`、`bug/`、`未命名.md`、`assets/` 大文件）卷进提交；`plugins/` 下新写的源码要提交。

## 6. 全局注意事项

- **MSVC 协程 ICE 风险**：`bug/msvc/coroutine-awaitable-ice/` 里有本项目踩过的 MSVC 协程 ICE 复现。**避免把 render 协程做成函数模板**——这是任务 01 选择"具体转发壳类型入参"而非"concept 约束的模板参数"的直接原因。
- 协程栈：`::bvn::gameplay::task`（include/bvn/gameplay/entity.h，基于 nagisa/concurrency + stdexec）。stop token 从协程自身 env 取（`co_await ::nagisa::concurrency::environment()` → `::stdexec::get_stop_token(env)`），已经打通，照现有 entity 写法抄即可。
- 三方库直接用、不包一层（engine-spec §6）；`renderer` / scheduler 抽象是为后端可换而留的例外。
