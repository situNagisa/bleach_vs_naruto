# GCC stdexec global module fragment ICE

Small dependency-based reproduction: `module.cpp` contains only the global module fragment, an include of `<stdexec/execution.hpp>`, and the module declaration. This is not yet a standalone reduction of stdexec itself.

## Run (PowerShell)

```powershell
./build.ps1 -Compiler 'path/to/g++.exe' -StdexecInclude 'path/to/stdexec/include'
```

`Compiler` defaults to `g++` on PATH. `STDEXEC_INCLUDE` can supply the include directory. The script first compiles `header.cpp` without modules, then compiles `module.cpp` with `-std=c++26 -fmodules`. Logs and module cache stay in `out/`; the script returns a nonzero exit code on compiler failure.

## Observed on 2026-10-21

GCC 16.1.0, WinLibs MinGW-W64 x86_64-msvcrt-posix-seh (Brecht Sanders r4):

- Plain header control: exit 0.
- Module reproduction: exit 1, internal compiler error (Segmentation fault).
- Diagnostic: `stdexec/__detail/__query.hpp:177`, `inline constexpr forwarding_query_t forwarding_query{};`.

This reproduces the failure that prevents building `demo.execution`, before reaching `renderer.cpp`. It isolates the module compilation path but does not identify the precise compiler defect. GCC 16.2 has not been tested: the release archive download failed during the investigation.

Dependency revision: NVIDIA/stdexec `633c87370b986735b0c8667993b5fc18bf1b3129`. No tracked dependency modifications were present during the test.

---

## 中文版

### GCC 在全局模块片段中包含 stdexec 时的内部编译器错误

这是仍依赖 stdexec 的精简复现：`module.cpp` 仅包含全局模块片段、`<stdexec/execution.hpp>` 头文件包含语句和模块声明。目前尚未将 stdexec 本身进一步缩减为无外部依赖的独立复现。

### 运行方式（PowerShell）

```powershell
./build.ps1 -Compiler 'path/to/g++.exe' -StdexecInclude 'path/to/stdexec/include'
```

`Compiler` 默认使用 PATH 中的 `g++`。也可通过环境变量 `STDEXEC_INCLUDE` 指定头文件目录。脚本先以普通翻译单元编译 `header.cpp`，再使用 `-std=c++26 -fmodules` 编译 `module.cpp`。日志和模块缓存保存在 `out/` 中；编译失败时脚本返回非零退出码。

### 验证结果（2026-10-21）

测试编译器为 GCC 16.1.0，发行包为 WinLibs MinGW-W64 x86_64-msvcrt-posix-seh（Brecht Sanders r4）：

- 普通头文件对照用例：退出码为 0，通过。
- 模块复现用例：退出码为 1，发生内部编译器错误（Segmentation fault，段错误）。
- 报错位置：`stdexec/__detail/__query.hpp:177`，对应 `inline constexpr forwarding_query_t forwarding_query{};`。

该复现重现了阻止 `demo.execution` 构建的错误，此时还未开始编译 `renderer.cpp`。目前已将触发条件缩小到模块编译路径，但尚未确定编译器内部的具体缺陷。GCC 16.2 尚未验证：调查期间发行包下载失败。

依赖版本为 NVIDIA/stdexec `633c87370b986735b0c8667993b5fc18bf1b3129`。验证时，该依赖仓库的已跟踪文件没有本地修改。
