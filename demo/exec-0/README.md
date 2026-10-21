# demo/exec-0 —— bvn 第一个完全用 C++20 模块组织的 demo

一个 sender/receiver 风格的帧循环：主线程用 `demo.input` 轮询键盘，渲染工作线程用 `demo.render`
走 Vulkan 1.3 dynamic rendering 清屏并呈现；窗口由 `demo.window`（直接包 GLFW 原生 API）提供，
两者共用 `demo.execution` 里对 stdexec 的类型擦除封装。本 demo 不依赖仓库的 `bvn::platform`（SDL）层。
它是 bvn 从“头文件 + .cpp”迁移到“具名模块”的样板：整个仓库里所有模块化代码都按这里的约定来写、来构建。

> 结论先行：**只用 Clang（≥ 22），C++ 标准永远最新，构建用 CMake（≥ 4.4）+ Ninja；
> Windows 上由 `bvn.slnx` 里的 Makefile 型工程 `exec-0.vcxproj` 调用 CMake 构建，F5 一键构建 + 调试，PDB 自动生成。
> 第三方库（stdexec、GLFW）直接放在家目录下，`NAGISA_LIBRARY_ROOT` 指向家目录。**

---

## 1. 工具链锚定与原因

| 决定 | 内容 | 原因 |
| --- | --- | --- |
| 编译器 | 只支持 Clang ≥ 22。Windows 用 LLVM 官方发行版的 `clang-cl.exe`（配 MSVC STL），Linux 用 `clang++`（配 libc++） | GCC 16 在全局模块片段里包含 stdexec 直接 ICE（[bug/gcc/stdexec-module-ice](../../bug/gcc/stdexec-module-ice)）；MSVC 14.51 导入含 `<stop_token>` 的模块报 C1116、协程 awaitable 报 C1001（[bug/msvc](../../bug/msvc)）。`CMakeLists.txt` 里对非 Clang 直接 `FATAL_ERROR` |
| C++ 标准 | `CMAKE_CXX_STANDARD 26`：clang 上是 `-std=c++26`，clang-cl 上是 `/std:c++latest` | 项目约定：编译器与标准永远跟踪最新版。CMake 新增更高的值（如 29）时，只改这一个数字 |
| 生成器 | 只允许 Ninja / Ninja Multi-Config | CMake 4.4 才支持 clang-cl 的模块扫描，并且[官方文档](https://cmake.org/cmake/help/latest/manual/cmake-cxxmodules.7.html)明确不支持 Visual Studio 生成器的 ClangCL 工具集。所以 VS 侧不让 CMake 生成工程，而是用 Makefile 型 `exec-0.vcxproj` 调用 CMake（§6） |
| `import std;` | 不用 CMake 自带的 `CMAKE_CXX_MODULE_STD`，而是 [cmake/std_module.cmake](cmake/std_module.cmake) 把标准库自带的 `std.cppm`（libc++）/ `std.ixx`（MSVC STL）编译成一个普通模块库目标 `exec0_std` | CMake 的 `import std` 支持仍是实验特性，开关 UUID 每个版本都变；而且只认 clang + libc++/libstdc++ 与 MSVC cl，**不支持 clang-cl + MSVC STL**。把 std 当普通模块编，对 CMake 来说毫无特殊之处，跨版本稳定 |

## 2. 目录结构与模块依赖

```
demo/exec-0/
├── CMakeLists.txt                  只负责装配：包含 cmake/ 下各文件，定义 exec0_demo
├── CMakePresets.json               共享预设（windows-clang-cl-* / linux-clang-*）
├── CMakeUserPresets.json.example   命令行用户的本机路径覆盖模板（复制为 CMakeUserPresets.json，已 gitignore）
├── exec-0.vcxproj                  Visual Studio 工程（Makefile 类型）：调用 CMake 构建、调试产物
├── exec-0.local.props.example      VS 工程的本机路径覆盖模板（复制为 exec-0.local.props，已 gitignore）
├── cmake/                          每个依赖 / 每类设置一个文件
│   ├── toolchain_guard.cmake       只允许 Clang ≥ 22 + Ninja
│   ├── build_settings.cmake        exec0_build_settings：警告、/EHsc、libc++、链接 /DEBUG
│   ├── std_module.cmake            找到 std.cppm / std.ixx 并建成 exec0_std 目标
│   ├── locate.cmake                bvn_exec0_locate()：环境变量 -> 缓存变量 -> 探测文件
│   ├── stdexec.cmake               exec0_stdexec（$NAGISA_LIBRARY_ROOT/stdexec，缺省 FetchContent）
│   ├── nagisa.cmake                exec0_nagisa（$NAGISA_ROOT）
│   ├── glfw.cmake                  glfw（$NAGISA_LIBRARY_ROOT/glfw 源码静态构建，缺省 find_package）
│   ├── vulkan.cmake                Vulkan::Headers / Vulkan::Vulkan（FindVulkan）
│   ├── vkfu.cmake                  vkfu::vkfu（仓库内 ../../vkfu）
│   └── vkkl.cmake                  exec0_vkkl（仓库内 include/vkkl）
├── main.cpp                        普通翻译单元，只 import，不再解析任何第三方头
├── execution/execution.cppm        module demo.execution ：唯一包含 stdexec 头的接口单元
├── window/window.cppm              module demo.window    ：glfw_context + window（GLFW 原生 API 的 RAII 包装）
├── window/window.cpp               demo.window 的实现单元（唯一调用 glfw* 函数的地方）
├── input/input.cppm                module demo.input     ：键盘快照 + 退出请求
├── input/input.cpp                 demo.input 的实现单元（只为 GLFW_KEY_* 常量包含 glfw3.h）
├── render/renderer.cppm            module demo.render    ：renderer / frame 租约池
└── render/renderer.cpp             demo.render 的实现单元（vkfu / vkkl / Vulkan）
```

模块依赖（箭头指向被导入方）：

```
std (exec0_std) ◄── demo.execution ◄── demo.input  ◄── main.cpp
                ◄── demo.window    ◄── demo.input
                    demo.window    ◄── demo.render ◄── main.cpp
                    demo.execution ◄── demo.render
```

仓库内的依赖以普通目标形式接入：`exec0_vkkl`（`include/vkkl`，header-only）、`vkfu::vkfu`（`add_subdirectory(../../vkfu)`）。
第三方：GLFW（家目录源码，静态）、Vulkan（SDK / 系统包）、stdexec 与 nagisa（家目录源码，header-only）。

## 3. 模块写法约定（从这个 demo 总结，后续迁移都照此）

1. **第三方头只在一个接口单元的全局模块片段里包含一次**，其他单元 `import`。例如 stdexec 只出现在
   `execution.cppm`。
2. **第三方符号在它自己的命名空间里重新导出，下游必须写全限定名。** `execution.cppm` 写的是
   `export namespace stdexec { using ::stdexec::just; … }`，所以使用处只能写 `::stdexec::just()`、
   `::stdexec::then(...)`、`::exec::static_thread_pool`，不存在裸的 `just` / `then`。stdexec 里 `exec` 只是
   `experimental::execution` 的命名空间别名，所以别名本身也要 `export namespace exec = ::experimental::execution;`。
3. **实现单元看得到接口单元的 import，看不到接口单元 `#include` 进来的名字。** `renderer.cpp` 不需要再
   `import std;` 或包含 stdexec，`window` 类型也因为接口 `import demo.window;` 而直接可见；但凡是它自己
   直接拼写的头文件里的名字（vkfu、vkkl、GLFW 宏）都必须再 `#include` 一次，否则 clang 报
   `missing '#include'; 'X' must be declared before it is used`。
4. **先 `#include` 后 `import std;`**：全局模块片段里的标准头必须出现在 `import std;` 之前；反过来不受支持。
5. **不要包含“囊括全部扩展”的总头。** `renderer.cpp` 只包含 `version_1_0/1_1/1_3`、`khr/swapchain`、
   `ext/debug_utils`、`layer/names` 六个 vkfu 头，而不是 `<vkfu/vulkan/v1.4.328.h>`。
6. **所有参与模块编译的目标都要链接 `exec0_build_settings`**（包括 `exec0_std`）。BMI 要求导出方与导入方
   的语言选项一致：标准、标准库（`-stdlib=libc++`）、异常模型（`/EHsc`）等；漏链会在导入时报 BMI 不兼容。
7. 跨模块传递的类型尽量是普通值或指针（`frame*` 作为租约令牌）；`export struct frame;` 是为了把 Vulkan
   对象的定义留在实现单元里，这是本 demo 里对编码规范“禁止前向声明”的唯一例外。

### 3.1 为什么是直接包 GLFW，而不是 SDL 或 NGS 的 glfw 封装

- 本 demo 只需要：创建一个无图形 API 的窗口、取 Vulkan 实例扩展、建 surface、查帧缓冲尺寸、泵事件、读按键、
  关闭请求。GLFW 原生 API 一一对应，`window/window.cpp` 一共不到 150 行。
- SDL3 覆盖面大得多（音频、手柄、渲染器……），从源码构建要拉一长串 Linux 开发库，编译也慢；仓库的
  `bvn::platform` 层继续用 SDL，本 demo 不再依赖它。
- [situNagisa/NGS](https://github.com/situNagisa/NGS) 里 `include/NGS/external/glfw/` 的封装已评估：2023 年的代码，
  硬依赖整个 NGS（bases / color / math / lib 宏），class + getter + `std::function` 回调表的写法与现行编码规范冲突，
  `create_surface` 失败只 assert 不抛异常；它没有超出 GLFW 原生 API 的增量价值，所以不引入。

## 4. 依赖清单

### 4.1 工具

| 工具 | 最低版本 | 说明 |
| --- | --- | --- |
| LLVM / Clang | 22 | Windows：从 [LLVM releases](https://github.com/llvm/llvm-project/releases) 安装官方 `LLVM-xx-win64.exe`（自带 `clang-cl.exe` 与模块扫描必需的 `clang-scan-deps.exe`）。**不要**用 VS 安装器里的“C++ Clang 工具”，版本落后。Linux：发行版 clang + 带模块清单的 libc++ |
| CMake | 4.4 | Windows：[cmake.org](https://cmake.org/download/) 安装包。VS 2022/2026 自带的 CMake 低于 4.4，VS 工程通过 `exec-0.local.props` 的 `BvnCMakeExe` 指向自装的版本（见 §6.1） |
| Ninja | 1.11 | VS 的“C++ CMake tools for Windows”组件自带；命令行使用时需在 PATH 上 |
| Visual Studio | 2022 17.x 或 2026 | 需要工作负载“使用 C++ 的桌面开发”，含 MSVC v14x 工具集（提供 MSVC STL、`modules\std.ixx`、`link.exe`）、Windows SDK、“C++ CMake tools for Windows”（只为了它自带的 Ninja） |
| Vulkan SDK | 1.3+ | Windows 上提供 Vulkan 头与 `vulkan-1.lib`，安装后有环境变量 `VULKAN_SDK`，FindVulkan 据此定位；也顺带装了验证层 |

### 4.2 库

| 库 | 形式 | 定位方式 |
| --- | --- | --- |
| stdexec | 家目录源码（header-only） | `$NAGISA_LIBRARY_ROOT/stdexec`，或 `-DBVN_EXEC0_STDEXEC_ROOT=<根>`；两者都没有时 FetchContent 拉取固定提交 `633c873`（与 bug/gcc 记录一致） |
| GLFW | 家目录源码，静态构建 | `$NAGISA_LIBRARY_ROOT/glfw`（`git clone https://github.com/glfw/glfw`），或 `-DBVN_EXEC0_GLFW_ROOT=<根>`；都没有时退回 `find_package(glfw3)`。Linux 上从源码构建需要 X11/Wayland 开发库（Arch：`libxi libxrandr libxinerama libxcursor libxkbcommon wayland-protocols`） |
| nagisa | 源码（header-only） | `$NAGISA_ROOT`，或 `-DBVN_EXEC0_NAGISA_ROOT=<根>`（需含 `include/nagisa/concurrency/lease.h`） |
| Vulkan headers + loader | 系统 | Windows：Vulkan SDK；Linux：`vulkan-headers` + `vulkan-icd-loader`。验证层可选，有则自动启用 |
| vkfu / vkkl / bvn::platform | 仓库内 | 相对路径 `../../` |

### 4.3 运行时

需要支持 Vulkan 1.3 且开启 `dynamicRendering` + `synchronization2` 的显卡驱动。没有驱动时程序在
`glfwGetRequiredInstanceExtensions` 或物理设备选择处抛异常，打印 `exec-0: ...` 后以退出码 1 结束。

## 5. 命令行使用

约定的目录布局（Windows 与 Linux 相同）：`NAGISA_LIBRARY_ROOT` 指向**家目录**，第三方库直接放在它下面；
nagisa 本身在 `NAGISA_ROOT`。

```
%USERPROFILE% / $HOME
├── stdexec/        git clone https://github.com/NVIDIA/stdexec
├── glfw/           git clone https://github.com/glfw/glfw
└── nagisa/         NAGISA_ROOT
```

把这两个变量设成系统环境变量（Windows：“编辑系统环境变量”；Linux：`~/.zshrc` 里 `export NAGISA_LIBRARY_ROOT="$HOME"`、
`export NAGISA_ROOT="$HOME/nagisa"`），VS 工程与命令行都会读。

### 5.1 Windows（clang-cl）

在 **“x64 Native Tools Command Prompt for VS”** 里执行（需要开发者环境提供 `VCToolsInstallDir`、`link.exe`、
`kernel32.lib`；普通 cmd / PowerShell 会在找 `std.ixx` 或链接时失败）：

```bat
cd demo\exec-0
cmake --preset windows-clang-cl-debug
cmake --build --preset windows-clang-cl-debug
out\build\windows-clang-cl-debug\exec-0.exe
```

LLVM 不在 `C:\Program Files\LLVM` 时，用 `CMakeUserPresets.json` 覆盖 `BVN_LLVM_ROOT`（§6.1），或者
`cmake --preset windows-clang-cl-debug -DCMAKE_CXX_COMPILER=D:/tools/LLVM/bin/clang-cl.exe`。

### 5.2 Linux（clang++ + libc++）

```bash
cd demo/exec-0
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
./out/build/linux-clang-debug/exec-0
```

`clang++ -print-file-name=libc++.modules.json` 必须返回绝对路径（Arch：`libc++` 包；Debian/Ubuntu：`libc++-XX-dev`）。

### 5.3 可覆盖的缓存变量

| 变量 | 作用 |
| --- | --- |
| `BVN_EXEC0_STD_MODULE_SOURCE` | 直接指定 `std.cppm` / `std.ixx` 的路径，跳过自动探测 |
| `BVN_EXEC0_STDEXEC_ROOT` / `BVN_EXEC0_GLFW_ROOT` / `BVN_EXEC0_NAGISA_ROOT` | 覆盖环境变量定位 |
| `CMAKE_CXX_COMPILER` | 换一个 clang / clang-cl |

## 6. 在 Visual Studio 中使用（目标：F5 = 构建 + 调试，与 MSBuild 工程无异）

VS 不生成、也不接管任何工程：`bvn.slnx` 里的 `demo/exec-0/exec-0.vcxproj` 是 **Makefile 类型**工程，
它的“生成”就是执行 `cmake --preset … && cmake --build --preset …`，它的“调试”就是启动 `NMakeOutput`
指向的 `out\build\<预设>\exec-0.exe`（PDB 在同目录）。MSBuild 工程的 F5 体验原样保留。

### 6.1 一次性准备

1. 安装 LLVM ≥ 22、CMake ≥ 4.4、Vulkan SDK、VS 组件（§4.1）；设置系统环境变量
   `NAGISA_LIBRARY_ROOT`、`NAGISA_ROOT`（§5）；把 stdexec、GLFW 克隆到家目录。
2. 把 [exec-0.local.props.example](exec-0.local.props.example) 复制为同目录的 `exec-0.local.props`（已 gitignore），
   按本机改这几个宏；不存在该文件时使用括号里的默认值：

   | 宏 | 含义 |
   | --- | --- |
   | `BvnLlvmRoot` | LLVM 安装根目录（默认 `C:\Program Files\LLVM`），工程把它导出成预设要读的 `BVN_LLVM_ROOT` |
   | `BvnCMakeExe` | CMake ≥ 4.4 的 `cmake.exe` 完整路径（默认 `cmake`，即 PATH 上的；VS 自带的版本太旧，不能用） |
   | `BvnNagisaLibraryRoot` / `BvnNagisaRoot` | 依赖根目录（默认取系统环境变量 `NAGISA_LIBRARY_ROOT` / `NAGISA_ROOT`） |

3. 不需要再改 `exec-0.vcxproj`。要改 CMake 预设名、构建目录等，改工程里 `UserMacros` 属性组的 `BvnCMakePreset`、`BvnBuildDir`。

### 6.2 打开、构建、调试

1. 双击 `bvn.slnx` 打开解决方案，解决方案资源管理器里右键 `exec-0` → **设为启动项目**。
2. 配置选 `Debug | x64` 或 `Release | x64`（分别对应预设 `windows-clang-cl-debug` / `windows-clang-cl-release`）。
3. **F5**：MSBuild 执行 `NMakeBuildCommandLine`——先 `cmake --preset`（首次配置，之后是空操作）再 `cmake --build`
   （Ninja 增量，只重编改动的模块及其导入者），然后加载 `exec-0.exe` 与 `exec-0.pdb` 启动调试器。
   断点、单步、监视、调用堆栈与普通 MSBuild 工程一致。
4. Ctrl+Shift+B 只构建；“重新生成”对应 `cmake --build --clean-first`；“清理”对应 `cmake --build --target clean`。
   CMake / Ninja / Clang 的输出都在 VS 的“输出”窗口里。

### 6.3 工程内部怎么把环境交给 CMake

`exec-0.vcxproj` 的 `BvnEnvSetup` 属性在执行 CMake 之前做四件事，改环境相关的问题先看它：

| 动作 | 为什么 |
| --- | --- |
| 把 `$(DevEnvDir)CommonExtensions\Microsoft\CMake\Ninja` 加进 PATH | 直接用 VS 自带的 Ninja，不必另装 |
| `set VCToolsInstallDir=$(VCToolsInstallDir)` | `cmake/std_module.cmake` 据此找 `modules\std.ixx`（MSBuild 属性不会自动变成环境变量） |
| `set BVN_LLVM_ROOT / NAGISA_LIBRARY_ROOT / NAGISA_ROOT` | 预设与 `cmake/*.cmake` 读的就是这几个环境变量 |
| `cd /d $(ProjectDir)` | `cmake --preset` 要在 `CMakePresets.json` 所在目录执行 |

PATH / INCLUDE / LIB 由 MSBuild 按所选工具集（`v145`）自动设置，所以 `link.exe`、`kernel32.lib`、MSVC STL 头都能找到。
IntelliSense 用的是 `NMakeIncludeSearchPath` / `AdditionalOptions`，只影响编辑器，不影响构建；VS 对模块的 IntelliSense
仍是实验性，红线不代表编译错误，以输出窗口为准。

### 6.4 调试参数、工作目录、环境变量

Makefile 工程的调试设置就是普通的“属性 → 调试”页：命令（默认 `$(NMakeOutput)`）、命令参数、工作目录
（工程里设为 `$(ProjectDir)`）、环境（例如 `VK_LOADER_DEBUG=all` 或 `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`）。
这些改动会写进 `exec-0.vcxproj.user`（已 gitignore，`*.user`），不会污染工程文件。

### 6.5 不要这样用

- 不要 `-G "Visual Studio 17 2022" -T ClangCL`：CMake 不支持该组合的模块扫描，`cmake/toolchain_guard.cmake` 会直接报错。
- 不要用 VS 自带的 clang-cl / CMake：版本低于 22 / 4.4 会被守卫拒绝。
- 不要把 `std.ixx` 复制进仓库：`cmake/std_module.cmake` 每次都从当前 MSVC 工具集取，保证与正在用的 STL 头一致。

## 7. 调试符号

| 平台 | 编译 | 链接 | 产物 |
| --- | --- | --- | --- |
| Windows clang-cl | `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=ProgramDatabase` → 所有配置都加 `/Zi`（clang-cl 即 CodeView 调试信息） | `exec0_build_settings` 对所有配置加 `/DEBUG`（Debug / RelWithDebInfo 本来就有，Release 由这里补上） | `out\build\<预设>\exec-0.pdb`，与 exe 同目录，VS 调试器自动加载；Debug 为 `/Od`，Release 为 `/O2` 带符号 |
| Linux clang++ | Debug 配置 `-g`（DWARF） | — | 调试信息内嵌在 `exec-0` 中，lldb/gdb 直接用 |

## 8. 编译时间（本机 Arch WSL2，clang 22.1.8，2 个并行任务）

| 单元 | 耗时 | 备注 |
| --- | --- | --- |
| 全量重编译（含 std 模块、4 个模块、4 个普通 TU、链接；不含 GLFW） | 约 10 s 墙钟 | `cmake --build --preset linux-clang-debug --clean-first`；GLFW 静态库首次构建另需几秒，之后不再重编 |
| `std.cppm` → `std.pcm` | 2.0 s | 只在标准库或编译选项变化时重编 |
| `execution.cppm`（含 stdexec） | 2.4 s | 全 demo 唯一解析 stdexec 的接口单元 |
| `renderer.cppm` / `renderer.cpp` | 2.7 s / 4.7 s | 后者解析 vkfu + vkkl；改为按需包含后不再解析所有厂商扩展 |
| `main.cpp` | 5.0 s | 不解析任何第三方头；`-ftime-trace` 显示 4.2 s 在前端，其中模板实例化 2.7 s、约束检查 2.1 s——这是 stdexec 算法（`let_value`/`starts_on`/`sync_wait`）在使用处实例化的固有成本，模块消掉的是解析成本 |

增量：只改 `main.cpp` 时仅重编它一个；改接口单元会连带重编所有导入者，所以接口里只放声明，实现尽量放实现单元。

## 9. 常见问题

| 现象 | 原因 / 处理 |
| --- | --- |
| `找不到标准库的 std 模块源文件` | Windows：没在 VS 开发者环境里（`VCToolsInstallDir` 未设置），用 x64 Native Tools 提示符或在 VS 内配置；Linux：没装带 `std.cppm` 的 libc++。也可直接 `-DBVN_EXEC0_STD_MODULE_SOURCE=...` |
| `LNK1104: cannot open file 'kernel32.lib'` / 找不到 `link.exe` | 同上，缺开发者环境 |
| `CMake was unable to find a build program corresponding to "Ninja"` | Ninja 不在 PATH；VS 内部会自动找到自带的 Ninja，命令行请自己加 PATH |
| `exec-0 只支持 Clang / clang-cl（当前：MSVC …）` | 预设里的 `BVN_LLVM_ROOT` 不对，或手动传了 `cl.exe` |
| `exec-0 需要 Ninja / Ninja Multi-Config 生成器` | 用了 VS 生成器；换 Ninja 预设 |
| 导入模块时报 BMI 与当前选项不兼容 | 某个目标漏链 `exec0_build_settings`，或改了编译选项后没有清理 `out/build/<预设>` |
| `missing '#include'; 'X' must be declared before it is used` | 实现单元直接用了接口全局模块片段里的名字，按 §3 第 3 条补 `#include` |
| 配置阶段报找不到 `Xinerama` / `xkbcommon` / `wayland-scanner` 等 | 从源码构建 GLFW 缺 Linux 开发库，按 §4.2 安装 |
| VS 里“生成”报 `cmake 不是内部或外部命令` | PATH 上没有 CMake ≥ 4.4；在 `exec-0.local.props` 的 `BvnCMakeExe` 写完整路径 |
| VS 里 F5 提示找不到 `exec-0.exe` | 构建失败了却没看输出窗口；或改了 `BvnBuildDir` / `OUTPUT_NAME` 但 `NMakeOutput` 没同步 |
| 运行报 `glfwGetRequiredInstanceExtensions failed` 或 `failed to select Vulkan physical device` | 没有 Vulkan 驱动 / loader，或驱动不支持 1.3 的 dynamicRendering / synchronization2 |

## 10. 本次验证范围与已知问题

- **已验证（Linux，clang 22.1.8 + libc++ 22.1.8 + CMake 4.4.4 + Ninja 1.13.2，依赖来自 `~/stdexec`、`~/glfw`、`~/nagisa`）**：
  Debug 与 Release 预设配置、全量编译、链接均通过且本仓库代码零警告；程序能启动并在无 Vulkan 驱动的机器上打印错误后干净退出。
- **未验证**：Windows 路径（clang-cl + MSVC STL `std.ixx` + GLFW 源码静态构建 + `exec-0.vcxproj`）只能在 Linux 上做静态设计，
  第一次在 Windows 上跑时最可能出问题的三处：`std.ixx` 在 clang-cl 下的编译（已加 `-Wno-reserved-module-identifier`、
  `-Wno-include-angled-in-module-purview`）；Makefile 工程执行 NMake 命令时的环境（§6.3 的 `BvnEnvSetup`，
  尤其 `VCToolsInstallDir` 与 Ninja 路径）；GLFW 静态库在 clang-cl 下的构建。
- **vkfu_gen 的缺陷（未在本 demo 中修改）**：用 Vulkan-Headers 1.4.363 编译时，`<vkfu/vulkan/v1.4.328.h>` 里 9 个
  `returnedonly` 结构的 `_vkfu_set_next(T&, void const*)` 把 `void const*` 赋给非 const 的 `pNext`，报
  `assigning to 'void *' from 'const void *' discards qualifiers`。涉及 `khr/acceleration_structure.h:191`、
  `arm/tensors.h:395`、`arm/data_graph.h:286/301/316/331`、`khr/surface_protected_capabilities.h:38`、
  `nv/optical_flow.h:116`、`nv/display_stereo.h:54`。本 demo 通过只包含用到的头规避；生成器应按结构的 `pNext`
  常量性生成形参类型。

## Windows vcpkg 配置

Windows 预设通过 `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` 启用 manifest 模式，使用 `x64-windows`。本目录的 `vcpkg.json` 声明 GLFW 和 Vulkan；首次配置自动安装到构建目录下的 `vcpkg_installed`，无需配置仓库根目录的全部游戏依赖。

在 `exec-0.local.props` 中设置 `BvnVcpkgRoot`（vcpkg 根目录），或设置环境变量 `VCPKG_ROOT`。VS 包装工程会将该属性传给 CMake。命令行使用者还需设置 `BVN_LLVM_ROOT` 并进入 x64 VS 开发者环境；用户预设示例包含这两个路径。

从没有 vcpkg 的旧缓存迁移时，首次运行 `cmake --fresh --preset windows-clang-cl-debug`（Release 同理），随后正常构建。Windows 预设显式选择 LLVM 的 `lld-link.exe`，避免 PATH 中的 MinGW `ld.exe` 被误选。
