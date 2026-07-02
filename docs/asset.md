# bvn 资源系统（asset）

> 讨论较少的 theme：讲清思路即可。承接 [engine-spec.md](engine-spec.md#bvn-引擎架构定稿engine-spec-v1) `assets` 模块 + [decisions.md §5 内容管线与数据](decisions.md#5-内容管线与数据t5)。

---

## 1. 思路

- **职责**：加载器 / 热重载 / 精灵表 / Lua 脚本化数据。
- **三方（直接用）**：stb（图像）、sol2 + lua。
- **加载策略**：**运行时直接加载 + 热重载**（直吃 BvN 精灵表 + 元数据）。
- **数据语言**：Lua 表（可脚本化数据，"能声明式就声明式"，便于将来编辑器 / 校验）。
- **资源所有权原则**：子对象依赖父对象资源时，优先把该资源**所有权移交给子对象**独立持有；不加"对比父版本号→重建"逻辑（见 [decisions.md §9 横切工程原则](decisions.md#9-横切工程原则)）。

> 数据 / Lua 热重载与 C++ 热重载的分工见 [plugin/hot-reload.md](plugin/hot-reload.md#热重载详解保留教学解释)。

---

## 2. 计划 / 阶段

- **Lua 时机**：语言现在定，深度（配置-only vs 纯 Lua 内容）随 M3 落地再定。
- Lua 相关（脚本化数据 / sol2 / lua）自 M3 起接入；M0–M2 零 Lua（C++ 热重载已覆盖大半迭代需求）。
- 里程碑全表见 [engine-spec.md §7](engine-spec.md#7-里程碑-模块映射)。
