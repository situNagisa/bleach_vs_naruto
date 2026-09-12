# bvn C++ 代码规范 v1

> 2026-06-15 · 配合 [game-design/design.md](game-design/design.md#bvn-游戏设计design)、[engine-spec.md](engine-spec.md#bvn-引擎架构定稿engine-spec-v1) 和 [decisions.md](decisions.md#bvn-技术架构决策日志)；文档索引见 [context.md](context.md#bvn-文档索引context)。

本文只记录 **bvn 项目的 C++ 限制和架构约定**。通用的现代 C++ 编码方式由全局 `modern-cpp-coding-standard` skill 提供；本文件作为项目 profile，在其基础上增加以下限制。本文约束引擎、插件、工具和测试中的全部 C++ 代码。

## 项目工具链与标准

- 项目目标为 C++26；MSVC 使用 `/std:c++latest`，Clang 使用 `-std=c++2c`。以当前目标实际启用的编译器能力为准。
- C++26 contracts、静态反射、`::std::execution`、`::std::println`、coroutine 和 `[[indeterminate]]` 在工具链可用后立即用于项目代码。
- CMake 要求 3.28 或更高版本。使用 Modern CMake 和 target 属性；命令使用小写，变量使用大写 snake_case。

## 命名空间与命名

- bvn 的公共代码位于 `::bvn` 或其模块命名空间中；引用 `std`、第三方库或其他根命名空间时必须从全局命名空间开始限定，例如 `::std`、`::boost`、`::bvn`。
- 普通标识符使用小写 snake_case；模板形参使用大驼峰；宏使用全大写 snake_case。
- 组件使用“裸名称 + 模块命名空间”的形式，不在名称中重复模块前缀。

## 文件与 Include

- 按目录和模块功能聚合文件，不强制一类型一文件；使用 `.h`/`.cpp` 和 `#pragma once`。
- 禁止前向声明；使用某个类型或函数时直接 Include 定义它的头文件。
- 模块内头文件使用相对路径（例如 `"rel.h"`）；跨模块头文件使用 `<bvn/mod/h.h>`。
- Include 分组顺序固定为：标准库、第三方库、bvn 跨模块、本模块；组间空行，组内不排序。
- 命名空间内的声明不缩进；缩进使用 Tab；行长不设硬上限。

## 类型与 API 组织

- bvn 类型统一使用 `struct`，默认不写访问修饰符。成员变量默认按面向使用者的私有接口处理：访问修饰保持 `public`，名称使用单下划线加小写（如 `_value`）。禁止 `_X`、`__x` 等保留标识符。
- 这里的“私有”是面向设计角色和使用者的可见性语义，不能机械地隐藏所有成员。成员服务外部使用者、外层类型或内部实现时，按其设计角色决定公开程度。
- 头文件中的 API 不再区分 `public/private API`；模板、concept 和 `constexpr` 实现放在头文件，其余实现放在 `.cpp`。
- 成员排列按可读性决定，不设固定顺序。
- 类的构造函数和其他特殊成员在不影响语义时保持平凡；无语义作用的特殊成员不要声明。

## 错误、所有权与生命周期

- 可恢复的运行时错误（资源缺失、加载失败、运行时非法状态）使用异常；逻辑错误使用 C++26 contracts，没有 contracts 时使用 `BVN_ASSERT` 或 `assert`；不可恢复错误终止程序。
- 异常不得抛出 `::std::logic_error`，因为它与 contracts 表达的逻辑错误相冲突。
- 有生命周期的资源必须由只移动的 RAII 所有者管理，构造时获取、析构时释放。调用点不逐个执行 teardown 或判空。
- 所有权优先移交，不通过版本比较执行重建。瞬态 GPU 资源作为 coroutine frame 内的局部量持有。

## ECS 与仿真

- ECS 使用 EnTT。裸 `::entt::registry` 就是 World；全局态放入 `reg.ctx()`。
- 系统按仿真流水线固定顺序执行；插件系统通过 coroutine 接入流水线。
- 组件可以自由组织为 `struct`，但必须遵循“裸名称 + 模块命名空间”的命名约定。

## 并发与任务图

- 加载、计算和渲染任务统一使用 sender/receiver 图，从 M0 起采用 NVIDIA stdexec 及 `::std::execution` 方向的接口。
- 线程只是 scheduler 的内部资源；调度器对外承诺串行或并行语义，不暴露固定线程模型。
- sender 的单发射、取消传播、共享和类型擦除规则以对应任务图实现及文档为准。

## 第三方库与 Vulkan

- 第三方库直接 Include、直接调用，不额外包一层。只有在明确需要可替换后端时，才允许建立抽象边界。
- Vulkan 句柄等 GPU 资源遵循 RAII 所有权规则；初始化只允许执行一次时用 contracts/assertion 表达不变式，不用分支静默跳过重复初始化。
- Vulkan、EnTT、stdexec 等库的项目使用方式以各自架构文档为准；编码风格冲突由本文和全局 skill 共同裁决，架构冲突由 `engine-spec.md` 和 `decisions.md` 裁决。

## DLL 导出

每个模块使用独立的导出宏，命名为 `BVN_<MODULE>_API`：

```cpp
#ifdef _WIN32
#  ifdef BVN_SIM_BUILD
#    define BVN_SIM_API __declspec(dllexport)
#  else
#    define BVN_SIM_API __declspec(dllimport)
#  endif
#else
#  define BVN_SIM_API
#endif
```

例如：`BVN_SIM_API`、`BVN_RENDER_API`、`BVN_GAMEPLAY_API`。

## 文档与暂缓决策

- 公有接口使用 Doxygen；实现内注释说明 why。
- 日志框架暂不定（当前使用 `::std::println`）；pmr 内存策略暂不定（默认 new/delete，按需引入）；测试规范在 M4+ 制定；Lua 编码规范在 M3 落地时制定。

> 冲突裁决：编码风格以全局 `modern-cpp-coding-standard` 和本文项目限制为准；架构以 [engine-spec.md](engine-spec.md#bvn-引擎架构定稿engine-spec-v1) 和 [decisions.md](decisions.md#bvn-技术架构决策日志) 为准。
