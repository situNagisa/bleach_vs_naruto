# bvn 文档索引（context）

> 给 agent 的快速索引：读一个 theme 时**只读它自己那一格指向的文件**，别牵扯无关文档。
> 每格写明：讲什么 / 不讲什么 / 何时读。文档规范（含权威裁决、命名、写作约束）见 doc-spec.md。

---

## 权威裁决（冲突时听谁的）

- **架构冲突** → engine-spec.md。**编码风格冲突** → coding-standard.md。**渲染子系统** → render/ 对应文档。
- `decisions.md` 与各 `decision.md`、各 `*-impl.md`、`vulkan-qa.md` 为**非权威**（记录为什么 / 现状 / 背景，不参与裁决）。

---

## 顶层（全局）

| 文件                   | 讲什么                                                    | 不讲什么                | 何时读             |
| -------------------- | ------------------------------------------------------ | ------------------- | --------------- |
| `engine-spec.md`     | **架构骨架唯一权威**：核心哲学、模块表、目录、关键接口契约、一帧数据流、设计准则、里程碑         | 子系统细节（只给指针）         | 想了解整体架构 / 动手前定位 |
| `coding-standard.md` | **C++ 编码唯一权威**：命名 / 错误处理 / RAII / DLL 宏 / C++26 等决策速查表 | 架构、玩法               | 写任何 C++ 代码前     |
| `decisions.md`       | 全局架构决策日志（T1–T9）：每条取舍与来龙去脉                              | 规范正文（在 engine-spec） | 想知道「为什么这么定」     |
| `net.md`             | 网络思路：host 权威 + 快照 + 预测和解，ENet，演进路线（讨论浅）                | 实现细节（未开工）           | 碰联网时（M6+）       |
| `asset.md`           | 资源系统思路：运行时加载 + 热重载、Lua 数据（讨论浅）                         | 加载器接口细节             | 碰资源 / Lua 时     |
| `animation.md`       | 动画：可选辅助件、暂缓；权威动画态进 sim/快照、帧数据时间线                       | 通用动画引擎（不做）          | 碰 2D 动画 / 帧数据时  |
| `platform.md`        | 平台层职责：窗口/输入/计时/fs/DLL 加载，SDL3（讨论浅）                     | 接口细节（未开工）           | 碰平台 / 输入时       |

## game-design/（游戏设计）

| 文件 | 讲什么 | 不讲什么 | 何时读 |
|---|---|---|---|
| `game-design/design.md` | **只讲"做什么"**：品类 / 视角 / 手感 / 英雄技能框架 / 可读性设计 | 实现、里程碑、技术选型、决策记录 | 想了解游戏本身 |
| `game-design/decision.md` | 游戏设计侧访谈记录 + 待决策清单 + A~G 层深化 | 架构决策（在 decisions.md） | 追游戏设计的取舍 |

## plugin/（插件）

| 文件 | 讲什么 | 不讲什么 | 何时读 |
|---|---|---|---|
| `plugin/spec.md` | 插件系统：万物皆插件、C++ 虚接口边界、每英雄一 DLL、manifest、加载（讨论浅） | C++ 热重载细节（在 hot-reload） | 碰插件 / 英雄加载时 |
| `plugin/hot-reload.md` | 热重载教学：加载≠热重载、vtable 悬垂、四条路、路 3(Live++) | 插件加载本身（在 spec） | 想搞清热重载为什么这么选 |

## client/（客户端）

| 文件 | 讲什么 | 不讲什么 | 何时读 |
|---|---|---|---|
| `client/impl.md` | client **现状**：entity 都放 client 下，模拟独立开发者产出 | 启动机制（在 render/boot） | 想知道当前有哪些 entity |

## render/（渲染子系统·讨论最详）

> 读渲染时按需取单文件。**设计/规范**看不带 impl 的；**现状/妥协**看 impl / vulkan-impl。

| 文件 | 讲什么 | 不讲什么 | 何时读 |
|---|---|---|---|
| `render/renderable.md` | renderable 概念（设计+规范）：render CPO、concept、核心契约、三层分离 | 运行时怎么跑（在 render-task / scheduler） | 想懂"一切可视物自己画自己" |
| `render/render-task.md` | render task（设计+规范）：render 协程形态、逐帧录制、env 取 stop、context dump | 帧编排、renderer 内部 | 写一个 render 协程时 |
| `render/renderer.md` | renderer（设计+规范）：形态职责、给 render task 的边界、五阶段**规范级** | 具体 vk 命令（在 vulkan-impl） | 想懂 renderer 是什么、怎么用 |
| `render/renderer-vulkan-impl.md` | renderer 的 **vulkan 实现（现状）**：为何先用 vulkan 充规范、五阶段 vk* 命令序列 | 规范级职责（在 renderer.md） | 动手写 vulkan 帧代码时 |
| `render/render-scheduler.md` | render scheduler（设计+规范）：两条 scheduler、submit() 一帧编排、契约、收尾、数据流 | 转发妥协（在 impl） | 想懂帧怎么被驱动 |
| `render/render-scheduler/model-ab.md` | 并发模型 A/B（设计）：并发事实、串行 primary vs 并行 secondary | 当前实现进度（在 impl） | 想懂为什么串行 / B 是方向 |
| `render/render-scheduler/impl.md` | render scheduler **现状+计划**：转发调度器妥协、context 取 scheduler、固化/并行/排序待办 | 终态设计（在 render-scheduler.md） | 想懂当前妥协与后续计划 |
| `render/boot.md` | 渲染侧启动：游戏主体只提供环境、职责二分、entity 注册 render task、启动次序 | entity 现状（在 client/impl） | 想懂渲染怎么被跑起来 |
| `render/vulkan-qa.md` | vulkan 概念背景（非规范）：GPU 异步 / in-flight / 帧槽 / dynamic rendering / 深度 hazard / B 示例代码 | 项目规范（在 renderer.md） | vulkan 基础不熟时补课 |

---

## 交叉引用约定

- 同目录文件用裸名（`renderer.md`）；跨目录用相对路径（`../animation.md`、`render/boot.md`）。
- 正文散文提到某文档不强制带路径；跳转性引用带路径 + 章节。
