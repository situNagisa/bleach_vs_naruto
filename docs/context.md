# bvn 文档索引（context）

> 给 agent 的快速索引：读一个 theme 时**只读它自己那一格指向的文件**，别牵扯无关文档。
> 每格写明：讲什么 / 不讲什么 / 何时读。文档规范（含权威裁决、命名、写作约束）见 [doc-spec.md](doc-spec.md#bvn-文档规范docspec)。

---

## 权威裁决（冲突时听谁的）

- **架构冲突** → [engine-spec.md](engine-spec.md#bvn-引擎架构定稿engine-spec-v1)。**编码风格冲突** → [coding-standard.md](coding-standard.md#bvn-c-代码规范-v1)。**渲染子系统** → [render/ 对应文档](#render渲染子系统讨论最详)。
- [decisions.md](decisions.md#bvn-技术架构决策日志) 与各 [decision.md](game-design/decision.md#bvn-游戏设计决策记录decision)、各 [*-impl.md](render/render-scheduler/impl.md#bvn-渲染render-scheduler-当前实现与妥协现状-计划)、[vulkan-qa.md](render/vulkan-qa.md#bvn-vulkan-概念问答qa) 为**非权威**（记录为什么 / 现状 / 背景，不参与裁决）。

---

## 顶层（全局）

| 文件                                                        | 讲什么                                                    | 不讲什么                                                              | 何时读             |
| --------------------------------------------------------- | ------------------------------------------------------ | ----------------------------------------------------------------- | --------------- |
| [engine-spec.md](engine-spec.md#bvn-引擎架构定稿engine-spec-v1) | **架构骨架唯一权威**：核心哲学、模块表、目录、一帧数据流、设计准则         | 子系统细节（只给指针）、里程碑（在 [roadmap.md](roadmap.md#bvn-路线图roadmap)）                                                       | 想了解整体架构 / 动手前定位 |
| [roadmap.md](roadmap.md#bvn-路线图roadmap) | 里程碑 M0–M9 → 模块映射 + 优先级 | 架构（在 [engine-spec.md](engine-spec.md#bvn-引擎架构定稿engine-spec-v1)） | 想知道当前阶段 / 下一步做什么 |
| [coding-standard.md](coding-standard.md#bvn-c-代码规范-v1)    | **C++ 编码唯一权威**：命名 / 错误处理 / RAII / DLL 宏 / C++26 等决策速查表 | 架构、玩法                                                             | 写任何 C++ 代码前     |
| [decisions.md](decisions.md#bvn-技术架构决策日志)                 | 全局架构决策日志（T1–T9）：每条取舍与来龙去脉                              | 规范正文（在 [engine-spec.md](engine-spec.md#bvn-引擎架构定稿engine-spec-v1)） | 想知道「为什么这么定」     |
| [simulator.md](simulator.md#bvn-仿真核心simulator)            | 仿真核心契约（规范草图）：裸 registry 即 World、共享交互底座、灵活属性表          | 玩法规则、渲染                                                           | 碰 sim / ECS / 英雄交互时 |
| [net.md](net.md#bvn-网络net)                                | 网络思路：host 权威 + 快照 + 预测和解，ENet，演进路线（讨论浅）                | 实现细节（未开工）                                                         | 碰联网时（M6+）       |
| [asset.md](asset.md#bvn-资源系统asset)                        | 资源系统思路：运行时加载 + 热重载、Lua 数据（讨论浅）                         | 加载器接口细节                                                           | 碰资源 / Lua 时     |
| [animation.md](animation.md#bvn-动画系统animation)            | 动画：可选辅助件、暂缓；权威动画态进 sim/快照、帧数据时间线                       | 通用动画引擎（不做）                                                        | 碰 2D 动画 / 帧数据时  |
| [platform.md](platform.md#bvn-平台层platform)                | 平台层职责：窗口/输入/计时/fs/DLL 加载，SDL3（讨论浅）                     | 接口细节（未开工）                                                         | 碰平台 / 输入时       |
| [doc-spec.md](doc-spec.md#bvn-文档规范docspec)                | 编辑docs时的要点                                             |                                                                   | 编辑docs时         |

## game-design/（游戏设计）

| 文件 | 讲什么 | 不讲什么 | 何时读 |
|---|---|---|---|
| [game-design/design.md](game-design/design.md#bvn-游戏设计design) | **只讲"做什么"**：品类 / 视角 / 手感 / 英雄技能框架 / 可读性设计 | 实现、里程碑、技术选型、决策记录 | 想了解游戏本身 |
| [game-design/decision.md](game-design/decision.md#bvn-游戏设计决策记录decision) | 游戏设计侧访谈记录 + 待决策清单 + A~G 层深化 | 架构决策（在 [decisions.md](decisions.md#bvn-技术架构决策日志)） | 追游戏设计的取舍 |

## plugin/（插件）

| 文件 | 讲什么 | 不讲什么 | 何时读 |
|---|---|---|---|
| [plugin/spec.md](plugin/spec.md#bvn-插件系统pluginspec) | 插件系统：万物皆插件、C++ 虚接口边界、每英雄一 DLL、manifest、加载（讨论浅） | C++ 热重载细节（在 [hot-reload.md](plugin/hot-reload.md#热重载详解保留教学解释)） | 碰插件 / 英雄加载时 |
| [plugin/hot-reload.md](plugin/hot-reload.md#热重载详解保留教学解释) | 热重载教学：加载≠热重载、vtable 悬垂、四条路、路 3(Live++) | 插件加载本身（在 [spec.md](plugin/spec.md#bvn-插件系统pluginspec)） | 想搞清热重载为什么这么选 |

## client/（客户端）

| 文件 | 讲什么 | 不讲什么 | 何时读 |
|---|---|---|---|
| [client/impl.md](client/impl.md#bvn-客户端现状clientimpl) | client **现状**：entity 都放 client 下，模拟独立开发者产出 | 启动机制（在 [render/boot.md](render/boot.md#bvn-渲染启动boot)） | 想知道当前有哪些 entity |

## render/（渲染子系统·讨论最详）

> 读渲染时按需取单文件。**设计/规范**看不带 impl 的；**现状/妥协**看 impl / vulkan-impl。

| 文件 | 讲什么 | 不讲什么 | 何时读 |
|---|---|---|---|
| [render/renderable.md](render/renderable.md#bvn-渲染renderable设计-规范) | renderable 概念（设计+规范）：render CPO、concept、核心契约、三层分离 | 运行时怎么跑（在 [render-task.md](render/render-task.md#bvn-渲染render-task设计-规范) / [render-scheduler.md](render/render-scheduler.md#bvn-渲染render-scheduler设计-规范)） | 想懂"一切可视物自己画自己" |
| [render/render-task.md](render/render-task.md#bvn-渲染render-task设计-规范) | render task（设计+规范）：render 协程形态、逐帧录制、env 取 stop、context dump | 帧编排、renderer 内部 | 写一个 render 协程时 |
| [render/renderer.md](render/renderer.md#bvn-渲染renderer设计-规范) | renderer（设计+规范）：形态职责、给 render task 的边界、五阶段**规范级** | 具体 vk 命令（在 [renderer-vulkan-impl.md](render/renderer-vulkan-impl.md#bvn-渲染renderer-的-vulkan-实现现状-实现)） | 想懂 renderer 是什么、怎么用 |
| [render/renderer-vulkan-impl.md](render/renderer-vulkan-impl.md#bvn-渲染renderer-的-vulkan-实现现状-实现) | renderer 的 **vulkan 实现（现状）**：为何先用 vulkan 充规范、五阶段 vk* 命令序列 | 规范级职责（在 [renderer.md](render/renderer.md#bvn-渲染renderer设计-规范)） | 动手写 vulkan 帧代码时 |
| [render/render-scheduler.md](render/render-scheduler.md#bvn-渲染render-scheduler设计-规范) | render scheduler（设计+规范）：两条 scheduler、submit() 一帧编排、契约、收尾、数据流 | 转发妥协（在 [impl.md](render/render-scheduler/impl.md#bvn-渲染render-scheduler-当前实现与妥协现状-计划)） | 想懂帧怎么被驱动 |
| [render/render-scheduler/model-ab.md](render/render-scheduler/model-ab.md#bvn-渲染并发模型-a-b设计) | 并发模型 A/B（设计）：并发事实、串行 primary vs 并行 secondary | 当前实现进度（在 [impl.md](render/render-scheduler/impl.md#bvn-渲染render-scheduler-当前实现与妥协现状-计划)） | 想懂为什么串行 / B 是方向 |
| [render/render-scheduler/impl.md](render/render-scheduler/impl.md#bvn-渲染render-scheduler-当前实现与妥协现状-计划) | render scheduler **现状+计划**：转发调度器妥协、context 取 scheduler、固化/并行/排序待办 | 终态设计（在 [render-scheduler.md](render/render-scheduler.md#bvn-渲染render-scheduler设计-规范)） | 想懂当前妥协与后续计划 |
| [render/boot.md](render/boot.md#bvn-渲染启动boot) | 渲染侧启动：游戏主体只提供环境、职责二分、entity 注册 render task、启动次序 | entity 现状（在 [client/impl.md](client/impl.md#bvn-客户端现状clientimpl)） | 想懂渲染怎么被跑起来 |
| [render/vulkan-qa.md](render/vulkan-qa.md#bvn-vulkan-概念问答qa) | vulkan 概念背景（非规范）：GPU 异步 / in-flight / 帧槽 / dynamic rendering / 深度 hazard / B 示例代码 | 项目规范（在 [renderer.md](render/renderer.md#bvn-渲染renderer设计-规范)） | vulkan 基础不熟时补课 |

---

## 交叉引用约定

- 同目录文件用裸名（`renderer.md`）；跨目录用相对路径（`../animation.md`、`render/boot.md`）。
- 正文散文提到某文档不强制带路径；跳转性引用带路径 + 章节。
