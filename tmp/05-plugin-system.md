# 任务 05：插件系统（扫描 + manifest + ABI 校验 + DLL 加载 + 工厂导出）

> 目标：按 [../plugin/spec.md](../plugin/spec.md) 落地插件基建第一版：开局扫描 `plugins/`（自包含文件夹 → DLL）+ 每插件 manifest（id/版本/ABI 整数）+ ABI 校验（不匹配拒载 + 日志）+ DLL 导出工厂产出协程；`plugins/test_basic` 做成真 DLL 打通全链路（DLL 内实体启动 render task 并画出东西）。
> 依据：plugin/spec.md（虚接口粗边界、宿主直接暴露 ECS/引擎内部、同工具链、明确不跨编译器）、engine-spec §4.8、平台层职责含 DLL 加载（platform.md）。
> 依赖：任务 01、02（要用当前 graphics 主线的 global/frame env 拆分与 `on_frame(pool, buffer)` awaitable；插件若要直接启动 render task，还需要把当前 client 内的 render workflow 宿主接口提升到共享头，见 §2）。

---

## 1. 现状与前置认知

- `plugins/test_basic/test_plugin.cpp` 目前是**空文件**，且被错误地编进主 exe（bvn.vcxproj 的 ClCompile 里）——要从主工程移除，改为独立 DLL 工程。
- 插件与宿主共享全部头文件、同工具链编译（spec 前提），因此 **C++ 类型（含协程 task、stdexec sender、entt registry）可直接过 DLL 边界**；一切协程/模板代码 header-only，无需宿主导出符号。
- 障碍：现在给 entity 用的 `context` / `render_workflow` 都在 `source/client/*.h` 的**匿名命名空间**里，DLL 看不见 → 需要先做一次共享头提升（§2）。

## 2. 前置重构：把插件要用的类型提升为共享头

1. **render workflow 宿主接口提升**：当前 `source/client/render_workflow.h` 仍在 client 内部；插件要启动 render task 时，需要一个共享头暴露最小宿主接口：`get_scheduler(workflow)` 透出 inner scheduler、`on_frame(pool, buffer)` 由 task 每帧调用、`submit()` 仍归宿主主循环驱动。不要恢复 task index / 注册表。
2. **事件通道抽出**：client context 里的 `{event_state events; mutex; revision; frame_time}` 四件套抽成 `include/bvn/platform/event.h` 里的一个小 struct（如 `event_channel`），context 持有之，插件经指针只读。
3. **宿主上下文（粗边界 struct）**：新头 `include/bvn/gameplay/plugin.h`：

```cpp
namespace bvn::gameplay
{
inline constexpr ::std::uint32_t plugin_abi = 1;      // 引擎 ABI 整数版本（spec §3）

using main_scheduler_type = decltype(::std::declval<::stdexec::run_loop&>().get_scheduler());

struct plugin_context        // 宿主 API：直接暴露引擎内部（spec §2），指针都由宿主保活
{
	::entt::registry* registry = nullptr;
	/* render_workflow host ptr */ void const* render_workflow = nullptr;   // 实现时换成共享头里的具体类型；submit 归宿主
	::exec::async_scope* render_scope = nullptr;       // 注意：热重载后改为插件私有 scope（任务 06）
	::exec::async_scope* main_scope = nullptr;
	main_scheduler_type main_scheduler{};
	::bvn::platform::event_channel const* events = nullptr;
};
}
```

4. **导出约定**（同头文件里给宏）：

```cpp
extern "C"
{
	// 插件必须导出这两个：
	//   bvn_plugin_abi()  -> ::std::uint32_t                    （加载后 double-check）
	//   bvn_plugin_main(plugin_context&) -> ::bvn::gameplay::task （工厂：产出插件主协程，宿主 spawn 到 main scheduler）
}
// 提供 BVN_PLUGIN(entry_fn) 宏展开出上面两个导出（dllexport；bvn_plugin_main 返回 C++ 类型
// 会触发 MSVC C4190 警告——同工具链下无害，宏里 #pragma warning(suppress: 4190)）。
```

## 3. platform 层：动态库加载

- 新文件 `include/bvn/platform/dynamic_library.h` + `source/platform/dynamic_library.cpp`：move-only RAII。
- 实现用 SDL3（platform 层已用 SDL）：`SDL_LoadObject / SDL_LoadFunction / SDL_UnloadObject`。接口：构造(path)（失败抛异常——运行时错误用异常）、`symbol(name) -> void*`、析构卸载。
- **shadow copy 加载**（为任务 06 铺路，本任务就做）：加载前把 DLL 拷到旁路名（如 `<dir>/.hot/<name>.<counter>.dll`）再 load——被加载的文件不落锁，重编译可直接覆盖原 DLL。counter 进程内递增。

## 4. manifest 与扫描

- 每插件文件夹放 `manifest` 文本（key=value，行注释 `#`，手写解析 ~30 行，不引 JSON 依赖）：

```
id = test_basic
version = 0.1.0
abi = 1
# binary 省略时约定为 bin/x64/<Debug|Release>/<id>.dll（与插件 vcxproj OutDir 约定一致）
```

- 宿主 `include/bvn/gameplay/plugin_host.h`（+cpp，归 gameplay 模块）：
  - `scan(plugins_dir)`：遍历一级子目录，读 manifest；缺 manifest 跳过。
  - 校验：`manifest.abi != plugin_abi` → **拒载 + `::std::println` 日志**（spec §3）；加载后再调 `bvn_plugin_abi()` double-check，不符即卸载拒载。
  - `load`：shadow copy → dynamic_library → 取 `bvn_plugin_main` → 记录 `loaded_plugin { manifest, dynamic_library, entry, ::exec::async_scope scope, 文件身份(写入时间+大小，任务 06 用) }`。
  - `start(plugin, plugin_context&)`：`scope.spawn(starts_on(main_scheduler, entry(ctx)))` ——**spawn 进插件私有 scope**（定向停机/热重载的前提），render task 由插件自己经 `starts_on(get_scheduler(*render_workflow), render(global))` spawn 进同一 scope。
- client main.cpp：context 建好、内建 entity spawn 完之后，`plugin_host.scan_and_start(BVN_PLUGIN_DIR 或 "../../../plugins" 相对 exe 的仓库路径——用 solution.props 加宏 `BVN_PLUGIN_ROOT=R"($(SolutionDir)plugins)"`，与 BVN_ASSET_ROOT 同款手法)`。

## 5. test_basic 插件

- 独立工程 `plugins/test_basic/test_basic.vcxproj`：
  - `ConfigurationType = DynamicLibrary`，`PlatformToolset v145`，import 与 bvn.vcxproj 相同的 props 链（nagisa.props / nagisa_library.props / solution.props），vcpkg manifest 同款开启（复用根 vcpkg.json：设 `VcpkgManifestRoot=$(SolutionDir)`）。
  - `OutDir = $(ProjectDir)bin\$(Platform)\$(Configuration)\`（与 §4 的路径约定咬合）。
  - include 路径同主工程（$(SolutionDir)include 等——照抄 bvn.vcxproj 生效的那套，必要时看 prop/ 与 nagisa props 提供了什么）。
  - 加入 `bvn.slnx`（新增 `<Project Path="plugins/test_basic/test_basic.vcxproj" />`）。主工程 bvn.vcxproj 的 ClCompile **移除** test_plugin.cpp。
- 内容（`test_plugin.cpp`，模式照抄 arena）：
  - 耐久态：POD `test_basic_state { float phase; ::std::uint64_t tick; }` 放 `registry.ctx()`。**类型定义放共享头**（如 `plugins/test_basic/test_basic_state.h` 且宿主不 include 也行——但见 §6 约束：ctx 里放插件私有类型会在卸载后悬垂 → 第一版把它定义在 `include/bvn/gameplay/plugin.h` 旁的共享头，或干脆只用协程局部 + 每次重载从 0 开始？**不行**，热重载要验证耐久态存续。→ 决策：放共享头 `include/bvn/gameplay/plugin_demo_state.h`，文档里注明这是演示用、真英雄状态归 M2 的 hero SDK 设计）。
  - 主协程：循环 `co_await schedule(main_scheduler)`，按 event_channel 的 frame_time 推 phase（旋转角）。
  - render task：用 grid shader（BVN_GRID_*_SPV 宏对插件同样生效，因为同 import solution.props）画一个绕原点旋转的线框方块，位置/角度取自耐久态；task 自己持有 secondary command pool / command buffer，并每帧调用 `on_frame(pool, buffer)`。
- **验证点**：方块出现在场景里且旋转 → DLL 内协程、ECS、render workflow、concept 转发壳全链路过边界成立。

## 6. 关键约束（写进正式文档）

- **插件写进 registry 的耐久态必须是共享头里定义的类型**（第一版铁律）：entt 的存储/析构函数指针跟随首次实例化的模块——插件私有类型进 registry 后卸载 DLL 即悬垂。真英雄的组件走 M2 hero SDK 共享头。
- 插件的一切并发工作必须 spawn 进**自己的 scope**（宿主给的 plugin scope），不得用全局 render_scope/main_scope——否则无法定向停机（热重载做不了）。plugin_context 里干脆不给全局 scope，只给插件自己的（上面 struct 里的 render_scope/main_scope 字段在实现时改成单一 `::exec::async_scope* scope`，命名自定，文档一致即可）。

## 7. 落地后要更新的正式文档

- [../plugin/spec.md](../plugin/spec.md)：仍是设计/规范，基本不动；§3 加载一节如与实现细节出入（manifest 键、路径约定）以"现状见 impl"指针带过。
- 新建 [../plugin/impl.md](../plugin/impl.md)（现状文档，doc-spec §1.2）：manifest 格式、目录/输出路径约定、导出约定（两个 extern "C" 符号 + 宏）、shadow copy 加载、per-plugin scope、共享头耐久态铁律、test_basic 现状。
- [../context.md](../context.md) plugin 表加 impl.md 一行。
- [../client/impl.md](../client/impl.md)：entity 清单补 test_basic（插件实体）一句 + 解掉文内 TODO 时顺手核对。

## 8. 验收

- 构建：`MSBuild bvn.slnx -p:Configuration=Debug -p:Platform=x64`（两工程都过）。
- 运行：日志打出扫描/加载/ABI 校验通过；场景里出现旋转线框方块；把 manifest 的 abi 改成 999 → 拒载 + 日志、游戏正常跑（无插件）。
- 关窗：干净退出（插件 scope 排空次序并入现有收尾），validation 无报错。
- commit 建议拆三笔：`render workflow 宿主接口提升为共享头`、`平台动态库加载 + 插件宿主（manifest/ABI/扫描/shadow copy）`、`test_basic 插件 DLL 全链路打通 + plugin/impl.md`。
