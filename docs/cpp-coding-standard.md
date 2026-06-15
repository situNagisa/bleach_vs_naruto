# bvn C++ 代码规范 v1

> 生成日期：2026-06-15
> 来源：基于逐项交互式问答确定，配合 `docs/game-design.md` · `docs/architecture.md` · `docs/engine-spec.md`
> 本规范为项目唯一 C++ 编码权威；所有 C++ 代码（引擎、插件、工具、测试）均受约束。

---

## 1. 命名规范

### 1.1 基本规则

全部采用 **C++ 标准库风格（全 snake_case）**，仅宏为例外。

| 实体 | 风格 | 示例 |
|---|---|---|
| 类型（class/struct/enum/concept/using） | snake_case | `hero_context`, `render_task`, `transform` |
| 函数/方法 | snake_case | `emit_hitbox()`, `capture_snapshot()`, `tick()` |
| 变量（局部/成员/参数） | snake_case | `move_dir`, `combo_count`, `current_tick` |
| 常量 | snake_case | `max_players`, `tick_rate` |
| 枚举值 | snake_case | `color::red`, `team::blue` |
| 宏 | UPPER_SNAKE_CASE | `BVN_REGISTER_HERO`, `BVN_ASSERT` |
| 命名空间 | snake_case | `bvn::sim`, `bvn::render`, `bvn::gameplay` |
| 文件名 | snake_case.h / snake_case.cpp | `hero_context.h`, `render_task.cpp` |

### 1.2 成员变量

无前缀、无后缀。靠上下文与 IDE 区分：

```cpp
struct transform
{
    float x{}, y{};      // 好
    // 不用 m_x, x_, _x 之类
};
```

### 1.3 禁止事项

- 禁止任何形式的匈牙利命名法
- 禁止类型前缀（`iCount`、`pData`、`fSpeed`）
- 禁止缩写唯我独懂（`cnt` → `count`, `ctx` → `context`；但 `ECS`、`GPU`、`RNG` 等行业通用缩写可用）

---

## 2. 格式化

### 2.1 缩进

**Tab 键**。一个 Tab = 一个缩进层级；宽度由编辑器决定。

### 2.2 大括号：Allman 风格

大括号**独占一行**。无一例外。

```cpp
void foo()
{
    if (x)
    {
        bar();
    }
}

struct transform
{
    float x{}, y{};
};
```

### 2.3 行长

**不设硬限制**。顺其自然；若一行太长影响可读性，自行换行。

### 2.4 空格

- `if`/`for`/`while`/`switch` 后留空格，左括号前留空格：`if (x)`, `for (auto i = 0; …)`。
- 二元运算符两侧留空格：`a + b`, `x == y`。
- 逗号 / 分号后留空格：`foo(a, b, c)`。
- 冒号（`:`）用在初始化列表 / 继承 / 访问修饰符时，前后留空格：`struct a : b {};`，`foo() : x(1), y(2) {}`。

### 2.5 空行

- 函数间一个空行。
- 逻辑段间一个空行。
- 命名空间内不缩进：

```cpp
namespace bvn::sim
{

void tick()
{
    // ...
}

} // namespace bvn::sim
```

---

## 3. 文件组织

### 3.1 文件粒度

**按目录功能聚合**，不强制"一个类型一个文件"。紧密相关的多个小类型可放同一文件。

### 3.2 扩展名

统一 `.h`（声明/接口）与 `.cpp`（实现）。

### 3.3 Include 路径

**同一模块内部**：引号 + 相对路径

```cpp
// 在 bvn/sim/src/foo.cpp 中 include 同模块的 bar.h
#include "bar.h"
```

**跨模块引用**：尖括号 + 模块路径

```cpp
#include <bvn/sim/registry.h>
#include <bvn/render/camera.h>
#include <entt/entt.hpp>
```

### 3.4 Include 顺序

分四层，层间空行，每层内不排序：

1. 标准库（C++ / C）
2. 第三方库
3. 本项目其他模块（`<bvn/…>`）
4. 本项目本模块文件（`"…"`）

```cpp
#include <cstdint>
#include <print>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <bvn/sim/transform.h>
#include <bvn/render/camera.h>

#include "my_local_helper.h"
```

### 3.5 头文件保护

一律用 **`#pragma once`**。

```cpp
#pragma once
// 头文件内容
```

### 3.6 前向声明

**禁止前向声明**。需要什么就 include 什么。不允许多个文件各自手写同一前向声明导致源码冗余。

---

## 4. 声明与修饰符

### 4.1 const 位置：东 const（右 const）

```cpp
int const x = 1;                     // const 在类型右侧
void foo(int const& ref);           // 引用也一样
auto const& get() const;            // 成员函数 const 也在右边
```

### 4.2 auto 使用：Herb Sutter 激进风

能用 `auto` 就用 `auto`。**不给 auto 加冗余修饰符**：

```cpp
auto* ptr = get_pointer();   // ❌ 冗余：auto 已经推导出指针
auto ptr = get_pointer();    // ✅

auto const& ref = get();     // ❌ 冗余：不用 auto const&
auto&& ref = get();          // ✅
auto ref = get_value();      // ✅ 返回值直接 auto（依赖 RVO）

auto& r = get_ref();         // ❌ 应该用 auto&&
auto&& r = get_ref();        // ✅
```

### 4.3 explicit：语义驱动

有意作为隐式转换的场景不加 `explicit`（如 `vec3` → `vec4` 提升），否则加。

```cpp
struct vec3
{
    /* implicit */ vec3(float s);              // 标量扩展，有意的隐式
};

struct non_copyable
{
    explicit non_copyable(int id);             // 构造不应该是隐式
};
```

### 4.4 noexcept：广泛标注

所有不抛异常的函数都显式标注 `noexcept`：

```cpp
auto get_entity_count() const noexcept -> std::size_t;   // getter
auto swap(a& other) noexcept;                             // swap
~foo() noexcept;                                           // 析构
```

### 4.5 [[nodiscard]]：跟标准库一致

类似 `std::expected`、`std::optional` 的模式，返回值「忽略即 bug」时才加：

```cpp
[[nodiscard]] auto acquire_resource() -> resource;   // 忽略即泄漏
auto get_position() const -> vec3;                    // 普通 getter 不加
```

### 4.6 变量初始化：必须初始化

声明时必须初始化。无法有意义初始化的用 C++26 **`[[indeterminate]]`** 标记：

```cpp
auto count = 0;                          // 好
auto const max = 100;                    // 好
float buffer[[indeterminate]];           // 暂不初始化的缓冲区（C++26）
```

优先用 `auto` 推导类型。

### 4.7 constexpr / consteval：激进

所有能在编译期算的，全部标 `constexpr`。充分利用 C++26 `constexpr` 扩展。

---

## 5. 类型系统

### 5.1 统一用 struct

不区分 `class`/`struct`，全用 **`struct`**。访问控制靠显式 `public:`/`private:`。

```cpp
struct hero_context
{
public:
    auto alive() const -> bool;

private:
    entity_id entity_;
};
```

### 5.2 枚举：全部 enum class

所有枚举都用 `enum class`，无隐式转换。位标记也先用 `enum class`，配合手写/宏生成的运算符。

```cpp
enum class team { blue, red, neutral };
enum class key_state { none, pressed, held, released };
```

### 5.3 类型别名

**标准库有的就用标准的**。标准库没有的才自己定义。

| 场景 | 用什么 |
|---|---|
| 32-bit 无符号整数 | `std::uint32_t` |
| 64-bit 有符号整数 | `std::int64_t` |
| 字节 | `std::byte` |
| 大小/索引 | `std::size_t`, `std::ptrdiff_t` |

### 5.4 整数符号：场景匹配语义

永远非负 → `unsigned`（如 `entity_id`、`count`）。可能为负 → `signed`。不搞"全 signed"或"全 unsigned"。

### 5.5 特殊成员函数：Rule of Zero + = delete

能用编译器默认的就用。不需要的明确 `= delete`：

```cpp
struct non_copyable
{
    non_copyable() = default;
    non_copyable(non_copyable const&) = delete;
    non_copyable& operator=(non_copyable const&) = delete;
    non_copyable(non_copyable&&) noexcept = default;
    non_copyable& operator=(non_copyable&&) noexcept = default;
};
```

### 5.6 成员排列

随意，按逻辑/可读性自行决定。`public`/`private` 次序不设限制。

---

## 6. 表达式与控制流

### 6.1 类型转换：仅 C++ 风格

禁止 C 风格 `(type)expr`。只用：

```cpp
static_cast<int>(x);
reinterpret_cast<std::uintptr_t>(ptr);  // 仅 GPU/序列化等底层场景
const_cast<char*>(s);                    // 极少用
```

### 6.2 迭代：优先 ranges/views

用 `std::views` 管道和 ranges 算法，取代传统 `for` 循环：

```cpp
// ✅ ranges 风格
auto alive = entities | views::filter(&is_alive) | views::transform(&get_position);

// ✅ 也接受（但 ranges 优先）
for (auto&& entity : reg.view<transform>().each())
{
    // ...
}
```

### 6.3 EnTT view：ranges 适配

```cpp
for (auto&& [entity, t, v] : reg.view<transform, velocity>().each())
{
    // 也可以用
}

auto positions = reg.view<transform>()
    | std::views::transform([&](auto e) { return reg.get<transform>(e).position; });
```

### 6.4 模板约束：全面 concept + requires

所有模板参数用 concept 约束。替换 enable_if / SFINAE / tag dispatch：

```cpp
template <typename T>
concept renderable = requires(T t, render_context ctx)
{
    { t.render(ctx) } -> std::same_as<void>;
};

auto submit(renderable auto&& task) -> void;
```

### 6.5 [[likely]] / [[unlikely]]：热路径积极使用

sim tick 热路径上明确标注分支预期：

```cpp
if (entity.is_alive()) [[likely]]
{
    resume_coroutine(entity);
}
else [[unlikely]]
{
    destroy_entity(entity);
}
```

### 6.6 using namespace：仅函数级局部

**严禁**在文件作用域（含 `.cpp`）和头文件中 `using namespace`。仅可在函数体内部局部使用：

```cpp
auto foo() -> void
{
    using namespace bvn::sim;    // ✅ 仅此函数内部有效
    auto& reg = get_registry();
}
```

命名空间别名（`namespace fs = std::filesystem`）允许在 `.cpp` 顶层使用。

---

## 7. 错误处理

### 7.1 唯一路径：异常

**统一使用异常**。不用 `std::expected`、不用错误码返回。

### 7.2 异常与 assert 的分工

- **异常**：处理运行时异常情况（资源找不到、加载失败、非法状态）。
- **断言**：检测程序逻辑 bug（不应发生的前置/后置/不变式）——与异常**不能相互替代**。

```cpp
auto load_texture(std::filesystem::path const& path) -> texture
{
    if (!exists(path))
    {
        throw resource_error{"texture not found: {}", path};   // 运行时异常情况
    }
    // ...
}

auto set_hp(entity e, float value) -> void
{
    BVN_ASSERT(value >= 0.0f, "HP must be non-negative");      // 逻辑 bug
    registry_.get<health>(e).hp = value;
}
```

### 7.3 断言：C++26 契约优先

优先用 C++26 契约（contracts）。契约不可用时降级为自定义 `BVN_ASSERT`：

```cpp
// C++26 契约（编译器支持时用）
void tick(int const dt)
    pre(dt > 0)
    post(old_hp >= 0)
{
    // ...
}

// 降级方案
void tick(int const dt)
{
    BVN_ASSERT(dt > 0, "dt must be positive");
    // ...
}
```

---

## 8. 值语义与参数传递

### 8.1 默认值语义

默认值传递/返回，依赖 RVO/NRVO。`std::move` 只在真正需要转移所有权时用。**不要**对返回值写 `std::move` 破坏 RVO：

```cpp
auto create_transform() -> transform
{
    transform t;
    // ...
    return t;              // ✅ RVO，不要 std::move(t)
}

auto process(vec3 v)       // ✅ 按值接收（sink）
{
    v.normalize();
    return v;
}
```

### 8.2 参数传递：语义驱动

不设硬性规则。视情况选择指针（可空语义）、引用（非空语义）、值（sink 语义）。以最贴切语义的方式为准。

### 8.3 隐式转换：语义驱动

与 `explicit` 规则一致——有意为转换的不阻止，无意的加 `explicit`。

---

## 9. 现代 C++ 习语

**越现代越好**。以下全部积极使用：

| 特性 | 示例 |
|---|---|
| CTAD（类模板参数推导） | `std::pair{1, 2.0}` |
| 指定初始化器 | `transform{.x=1.0f, .y=2.0f}` |
| 结构化绑定 | `auto [x, y] = get_position();` |
| if/for 初始化语句 | `if (auto it = map.find(k); it != map.end())` |
| using enum | `using enum team;` → 直接用 `blue`/`red` |

### 9.1 尾置返回类型

自由选择。不设统一规则。

---

## 10. 模块化与可见性

### 10.1 API 可见性：全公开

头文件全部公开。不强制区分 public/private API。用不用由调用者决定。

### 10.2 头文件实现

模板、concept、constexpr 函数自然在头文件。其余放 `.cpp`。不强求所有短函数在头文件实现。

### 10.3 DLL 导出：每模块独立宏

每个模块定义自己的导出宏：

```cpp
// sdk/include/bvn_sdk/export.h
#pragma once
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

模块命名约定：`BVN_<MODULE>_API`（如 `BVN_SIM_API`、`BVN_RENDER_API`、`BVN_GAMEPLAY_API`）。

---

## 11. C++26 特性策略

### 11.1 态度：全面铺开

C++26 核心特性作为一等公民，能用就用：

- **契约（contracts）**：替代 assert / 前置后置条件检查
- **静态反射（static reflection）**：ECS 元数据、序列化、插件元数据
- **`std::execution`（senders/receivers）**：统一并发模型（CPU 线程池 + CUDA scheduler 同构）
- **`std::println`**：M0–M2 日志方案
- **`[[indeterminate]]`**：延迟初始化标记

### 11.2 并发：全面 sender/receiver 图

所有异步都用 sender/receiver 图：资源加载、计算 offload、渲染任务图。从 M0 起即以 sender/receiver 为并发模型。

---

## 12. ECS 规范（EnTT）

### 12.1 组件命名

**裸名 + 所在模块命名空间**：

```cpp
namespace bvn::sim
{
struct transform { f32 x{}, y{}, z{}; };
struct velocity { f32 dx{}, dy{}; };
} // namespace bvn::sim

namespace bvn::gameplay
{
struct health { f32 current{}, max{}; };
struct team_index { std::uint8_t id; };
} // namespace bvn::gameplay
```

### 12.2 组件定义：自由

`struct` 形式随意决定。允许聚合、允许 helper 方法、允许 traits tag。不设限制。

### 12.3 世界访问

`entt::registry` 直接暴露为世界。所有人直接读写。无 World 包装类。全局态放 `reg.ctx()`。

### 12.4 系统流水线

引擎固定排序，插件不直接插系统——通过协程接入。

---

## 13. 协程规范（英雄 = 协程）

### 13.1 组织形式：自由函数 + ctx

每个英雄一个协程**自由函数**，全通过 `hero_context` 访问世界：

```cpp
// 英雄 = 自由函数协程（引擎每 tick resume）
hero_task kenpachi(hero_context ctx)
{
    int combo = 0;                              // 瞬态 = 协程局部变量
    while (ctx.alive())
    {
        co_await ctx.next_tick();
        auto& in = ctx.input;
        if (in.pressed(key::j))
        {
            co_await ctx.play("slash1", 3);     // startup
            ctx.emit_hitbox(/*...*/);
            co_await ctx.wait(3);
            ++combo;
            co_await ctx.play("recover", 5);
        }
        ctx.move(in.move_dir);                  // 写 ECS（耐久态）
        co_yield ctx.render_task(/*...*/);
    }
}
BVN_REGISTER_HERO(kenpachi, "kenpachi");
```

### 13.2 状态二分

- **耐久/可见/联网** 的状态 → ECS 组件（快照只抓 registry）
- **瞬态/控制流** 的状态 → 协程局部变量（连段计数、蓄力计时等）

### 13.3 封装自由度

允许在自由函数协程之外使用轻量 struct 组织 helper、或暴露具名钩子。不强制纯函数风格。

---

## 14. 第三方库使用

### 14.1 直接使用

第三方库头文件直接 include、API 直接调用。**不包一层**（除非为后端可换的设计目标）。

### 14.2 需要抽象的几个例外

仅为**设计目标**保留的抽象层（这些是为后端可换做的设计决策，不是因为不信任库）：

| 抽象 | 目的 |
|---|---|
| `IRenderer` | Vulkan → CUDA 后端切换 |
| `INetTransport` | ENet → 后期升级 netcode |
| `ComputeScheduler` | CPU 线程池 → nvexec CUDA scheduler |

其余三方库直接使用。

---

## 15. CMake 规范

### 15.1 风格

- **Modern CMake**：target 属性为主，少用全局设置
- **命令小写**：`add_executable`，不是 `ADD_EXECUTABLE`
- **变量 UPPER_SNAKE_CASE**

```cmake
add_library(bvn_sim STATIC)
target_sources(bvn_sim PRIVATE src/tick.cpp src/snapshot.cpp)
target_include_directories(bvn_sim PUBLIC include)
target_link_libraries(bvn_sim PUBLIC bvn_foundation EnTT::EnTT)
```

### 15.2 最低版本

CMake 3.28+（配合 C++26 工具链与 vcpkg）。

---

## 16. 文档（Doxygen）

### 16.1 公有接口：Doxygen 注释

```cpp
/**
 * @brief  执行一个 sim tick。
 * @param  reg   世界 registry
 * @param  inputs 本 tick 的输入命令集
 * @throws sim_error 若 registry 状态不一致
 */
auto tick(entt::registry& reg, std::span<input_command const> inputs) -> void;
```

### 16.2 内部实现

鼓励 `//` 注释说明 **why**（非 what）。关键设计决策处加注释。

---

## 17. 暂缓事项

以下在初期（M0–M3）暂不确定详细规范，后续补充：

- **日志框架**：M0–M2 用 `std::println` 直接输出终端，日志框架 M4+ 再定
- **pmr 内存策略**：当前默认 new/delete，pmr 在需要时再引入
- **测试规范**：测试框架和规范推迟到 M4+ 确定
- **Lua 编码规范**：Lua 集成在 M3 落地，届时补充 Lua 侧规范

---

## 附录 A · 决策速查表

| 项目 | 决策 |
|---|---|
| 命名风格 | 全 snake_case（仅宏 UPPER_SNAKE） |
| 缩进 | Tab |
| 大括号 | Allman（独占一行） |
| 行长 | 不设硬限制 |
| 头文件保护 | `#pragma once` |
| Include 风格 | 模块内 `"rel.h"` → 跨模块 `<bvn/mod/header.h>` |
| Include 顺序 | 标准库 → 三方 → 本项目跨模块 → 本项目本模块 |
| const | 东 const（`int const&`） |
| auto | 激进，不加冗余修饰符 |
| 类型声明 | 统一 `struct` |
| 枚举 | 全部 `enum class` |
| 类型别名 | 标准库优先使用，浮点用 `f32` |
| 转换 | 仅 `static_cast`/`reinterpret_cast`/`const_cast` |
| 错误处理 | 纯异常 + C++26 契约 / BVN_ASSERT |
| noexcept | 广泛标注 |
| [[nodiscard]] | 标准库风（忽略即 bug 时才加） |
| constexpr | 激进 |
| 模板 | 全面 concept + requires |
| 迭代 | 优先 ranges/views |
| using namespace | 仅函数级局部 |
| DLL 导出 | 每模块独立宏（`BVN_SIM_API` 等） |
| 协程 | 自由函数 + ctx 驱动 |
| ECS 组件 | 裸名 + 模块 namespace |
| 并发 | 全面 sender/receiver 图 |
| 三方库 | 直接使用，不包一层（除设计要求的抽象） |
| CMake | Modern + 小写命令 + target 属性 |
| 文档 | Doxygen 公有接口 |
| C++26 | 全面铺开（contracts/reflection/senders/println/[[indeterminate]]） |
| 前向声明 | 禁止；直接 include |

---

> 本规范随项目演进持续迭代。与 `engine-spec.md` 冲突以本规范为准（编码风格层面）；与 `architecture.md` 冲突以 `engine-spec.md` 为准（架构决策层面）。
