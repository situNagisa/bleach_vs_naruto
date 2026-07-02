# bvn 平台层（platform）

> 讨论较少的 theme：native 平台处理。承接 [engine-spec.md §2 模块](engine-spec.md#2-模块细粒度各独立库heroes-为-dllclientserver-为可执行) `platform` 模块。

---

## 思路

- **职责**：系统层——窗口 / 输入 / 计时 / 文件系统 / **动态库加载**（插件 DLL 由它加载）。
- **三方（直接用）**：SDL3。
- **无头约束**：`apps/server` 不链 platform 的窗口部分。
- **输入抽象**：对上暴露"移动向量 / 瞄准 / 技能触发"，不写死按键（手柄暂不做，低成本预留）。

<!-- TODO(补全): 待实现推进后补充 window/input/timer/fs/dll-load 各自的接口形态。当前仅记录职责与依赖。 -->
