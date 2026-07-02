# bvn C++ 代码规范 v1

> 2026-06-15 · 配合 game-design/design.md / engine-spec.md / decisions.md；文档索引见 context.md
> 项目唯一 C++ 编码权威，约束所有 C++ 代码（引擎 / 插件 / 工具 / 测试）。
> 总原则：**越现代越好 · 最小仪式 · 三方库直接用**。
> 下表即完整规则；其后只展开少数需要解释的点。

## 决策速查表

| 项目                          | 决策                                                                                                                                                        |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 命名                          | 全 **snake_case**；仅宏 UPPER_SNAKE（`BVN_REGISTER_HERO`）。无匈牙利 / 类型前缀 / 成员前后缀。行业缩写（ECS/GPU/RNG）可用，其余不缩写。最外层命名空间需要用全局命名空间限定符，如`::std`，第三方库也是，如`::boost`，`::bvn` |
| 缩进 / 大括号 / 行长               | **Tab**；**Allman**（大括号独占一行，无例外）；行长不设硬限。                                                                                                                   |
| 空格 / 空行                     | `if (x)`、`a + b`、`foo(a, b)`、`a : b`；命名空间内**不缩进**；函数间 / 逻辑段间一空行。                                                                                          |
| 文件                          | 按目录功能聚合（不强制一类型一文件）；`.h`/`.cpp`；`#pragma once`；**禁前向声明**（要啥 include 啥）。                                                                                    |
| Include                     | 模块内 `"rel.h"`，跨模块 `<bvn/mod/h.h>`；顺序 标准库 → 三方 → 本项目跨模块 → 本模块，层间空行、层内不排序。                                                                                  |
| const                       | **东 const**（`int const&`）。                                                                                                                                |
| auto                        | 激进，能用就用，**不加冗余修饰符**（见下）。                                                                                                                                  |
| 初始化                         | 声明即初始化；无法初始化用 C++26 `[[indeterminate]]`。                                                                                                                  |
| constexpr / consteval       | 激进：凡能编译期算的都标。                                                                                                                                             |
| explicit / 隐式转换             | 语义驱动：有意的隐式（如 `vec3`→`vec4`）不加，无意的加 `explicit`。                                                                                                            |
| noexcept                    | 不抛的函数广泛标注。                                                                                                                                                |
| `[[nodiscard]]`             | 标准库风：忽略即 bug 时才加。                                                                                                                                         |
| 类型声明                        | 统一 `struct`；成员默认公开，不写 `public:` / `private:` / `protected:` 访问限定。                                                                                         |
| 枚举                          | 全 `enum class`（位标记也是，配手写 / 宏生成运算符）。                                                                                                                       |
| 类型别名                        | 标准库优先（`std::uint32_t`/`std::byte`/`std::size_t`…）；整数符号按语义（非负 `unsigned`，可负 `signed`）。                                                                     |
| 特殊成员                        | Rule of Zero + 不需要的 `= delete`。                                                                                                                           |
| 成员排列                        | 随意，按可读性。                                                                                                                                                  |
| 类型转换                        | 仅 C++ 风格（`static_cast` / `reinterpret_cast` / `const_cast`），禁 C 风格 `(T)x`。                                                                                |
| 迭代                          | 优先 `ranges`/`views`（兼容 EnTT view）。                                                                                                                        |
| 模板                          | 全面 concept + `requires`，替代 SFINAE / `enable_if`。                                                                                                          |
| `[[likely]]`/`[[unlikely]]` | sim 热路径积极标注。                                                                                                                                              |
| `using namespace`           | 仅函数体内局部；别名（`namespace fs = …`）可在 `.cpp` 顶层。                                                                                                               |
| 值语义                         | 默认值传 / 值返回靠 RVO（**别对返回值 `std::move`**）；参数传递语义驱动（指针=可空 / 引用=非空 / 值=sink）。                                                                                  |
| 现代习语                        | CTAD、指定初始化、结构化绑定、if/for 初始化语句、`using enum` 积极用；尾置返回类型自由。                                                                                                  |
| API 可见性                     | 头文件全公开，不分 public/private API；模板 / concept / constexpr 在头，其余在 `.cpp`。                                                                                      |
| 错误处理                        | **纯异常**（不用 `expected` / 错误码）；异常处理运行时情况、断言查逻辑 bug，二者不互替（见下）。                                                                                               |
| 三方库                         | 直接 include、直接用、**不包一层**；唯一例外是为"后端可换"的设计抽象（见下）。                                                                                                            |
| ECS                         | 组件 = 裸名 + 模块 namespace，struct 形式自由；**裸 `entt::registry` 即世界**，全局态放 `reg.ctx()`；系统固定流水线，插件走协程。                                                             |
| 并发                          | 全面 sender/receiver 图（加载 / 计算 / 渲染任务），M0 起。                                                                                                                |
| DLL 导出                      | 每模块独立宏 `BVN_<MODULE>_API`（见下）。                                                                                                                            |
| CMake                       | Modern（target 属性为主）、命令小写、变量 UPPER_SNAKE；≥ 3.28。                                                                                                           |
| 文档                          | 公有接口 Doxygen；实现内 `//` 说明 **why**。                                                                                                                         |
| C++26                       | 全面铺开：contracts / 静态反射 / `std::execution` / `std::println` / `[[indeterminate]]`。                                                                          |

## 需要展开的几点

**auto —— 不加冗余修饰符**
```cpp
auto p = make_pointer();   // 不写 auto*
auto&& r = borrow_ref();   // 引用一律 auto&&（不写 auto& / auto const&）
auto v = make_value();     // 值，靠 RVO
```

**错误处理 —— 异常 vs 断言（不可互替）**
- **异常** = 运行时异常情况（资源缺失 / 加载失败 / 非法状态）。
- **断言** = 逻辑 bug（前置 / 后置 / 不变式）：优先 C++26 `pre()/post()`，降级 `BVN_ASSERT`。
- **初始化守卫是不变式，用断言不用 `if`**：只应初始化一次的资源，用 `assert(handle == VK_NULL_HANDLE)` 表达前置不变式、再无条件创建；不要用 `if (handle == VK_NULL_HANDLE) { create... }` 把"重复初始化"这种逻辑错误变成静默跳过。
- **有生命周期的资源用 RAII 拥有者收尾，不在调用点逐个判空**：用 move-only 的 RAII 拥有者（构造即获取、析构即释放）管理有生命周期的对象（如 GPU 句柄），于是不必在每个调用点写 `if (handle != VK_NULL_HANDLE) destroy`——判空只写进 wrapper 的析构一次。清理期判空本身不是逻辑错误（对象可能创建到一半就抛了），但用 RAII 就根本不必在调用点写它。瞬态 GPU 资源应作为协程帧内的局部量持有（见 render/render-task.md 的 render task 形态）。

```cpp
auto pipeline = make_pipeline(device);   // RAII 拥有者：构造即建、析构即毁
assert(other == VK_NULL_HANDLE);         // 只该建一次 → 断言，不 if
other = create_other(device);
// 退出作用域：pipeline 自动销毁，无手动 teardown、无逐点判空
```

> 所有权层面的决策（"优先移交所有权、不做版本对比重建"）属架构决策，见 decisions.md 的「横切工程原则」。

**DLL 导出宏 —— 每模块一个**
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
// 命名约定：BVN_SIM_API / BVN_RENDER_API / BVN_GAMEPLAY_API …
```

## 暂缓（M0–M3 后再定）
日志（先 `std::println`，M4+ 定框架）· pmr 内存策略（默认 new/delete，按需引入）· 测试规范（M4+）· Lua 编码规范（M3 落地时定）。

> 冲突裁决：**编码风格**层面以本规范为准；**架构**层面以 `engine-spec.md` 为准。
