# MSVC stop_token module import regression

Standalone standard-library-only reproduction. No renderer, Vulkan, Nagisa or stdexec dependency.

## Run

From an x64 Visual Studio Developer Command Prompt:

```bat
build.bat
build.bat control
```

The default builds `stop_origin.cppm`, `stop_middle.cppm`, then `use.cpp`.
The control omits `<stop_token>` from the intermediate module. Build products stay in `out/failing` or `out/control`; compiler failure is returned as the script exit code.

## Observed on 2026-10-21

| Toolset | Default | Control |
| --- | --- | --- |
| MSVC 14.51.36231 | C1116 on importing `stop_origin` through `probe` | Pass |
| MSVC 14.44.35207 | Pass | Not required for comparison |

Failure:

```text
include/stop_token(248): fatal error C1116
std::_Stop_callback_base::_Do_attach specialization with argument false
```

`stop_origin` instantiates `std::stop_callback`; the intermediate module includes `<stop_token>` in its global module fragment and imports `stop_origin`. Importing the intermediate module fails while reading the specialization. Removing the repeated header include makes the same 14.51 test pass.

This isolates an IFC import/declaration-merging regression. It does not establish the exact internal compiler defect. The full renderer triggers C1001 in `module/reader.cpp:4428`; this reduction produces the more specific C1116.

Select toolset 14.44 through `vcvars64.bat -vcvars_ver=14.44` in a fresh command prompt to run the comparison. No toolchain installation path is hardcoded in the script.

---

## 中文版

### MSVC stop_token 模块导入回归

这是仅依赖 C++ 标准库的独立复现，不依赖 renderer、Vulkan、Nagisa 或 stdexec。

### 运行方式

在 x64 Visual Studio 开发人员命令提示符中运行：

```bat
build.bat
build.bat control
```

默认用例依次编译 `stop_origin.cppm`、`stop_middle.cppm` 和 `use.cpp`。对照用例移除了中间模块中的 `<stop_token>` 包含语句。构建产物分别位于 `out/failing` 和 `out/control`；编译失败时，脚本会返回编译器的非零退出码。

### 验证结果（2026-10-21）

| 工具集 | 默认复现 | 对照用例 |
| --- | --- | --- |
| MSVC 14.51.36231 | 经由 `probe` 导入 `stop_origin` 时出现 C1116 | 通过 |
| MSVC 14.44.35207 | 通过 | 未单独验证，默认用例已足以进行版本对比 |

错误信息：

```text
include/stop_token(248): fatal error C1116
std::_Stop_callback_base::_Do_attach specialization with argument false
```

`stop_origin` 实例化了 `std::stop_callback`；中间模块在全局模块片段中包含 `<stop_token>`，并导入 `stop_origin`。下游导入中间模块时，编译器读取上述模板特化失败。在同一 14.51 工具集中，移除中间模块重复包含头文件的语句后即可通过。

该复现将问题缩小到 IFC 导入或声明合并方面的回归，但尚未确定编译器内部的具体缺陷。完整 renderer 在 `module/reader.cpp:4428` 触发 C1001；这个精简复现给出了更具体的 C1116。

需要对比 14.44 时，在新的命令提示符中通过 `vcvars64.bat -vcvars_ver=14.44` 选择工具集后运行脚本。构建脚本未硬编码工具链安装路径。
