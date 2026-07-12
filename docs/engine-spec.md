# bvn 引擎架构定稿（Engine Spec v1）

> 日期：2026-06-15　状态：**定稿 · 架构唯一权威**（经逐项问答确定）
> **架构冲突一律以本文为准；编码风格以 [coding-standard.md](coding-standard.md#bvn-c-代码规范-v1) 为准。** 决策取舍见 [decisions.md](decisions.md#bvn-技术架构决策日志)，文档索引见 [context.md](context.md#bvn-文档索引context)，里程碑见 [roadmap.md](roadmap.md#bvn-路线图roadmap)。
> 本文是**全局架构骨架**：核心哲学、模块表、目录、一帧数据流、设计准则；子系统（渲染 / 仿真 / 网络 / 插件 / 动画 / 资源 / 平台）在此只给骨架 + 指针，细节见各 theme 文档。

---

## 1. 核心哲学（一切的定调）

**引擎 = 平台，只干三件事：**
1. **渲染**英雄表达的东西；
2. 把**输入 / 交互**喂给英雄；
3. 处理**"英雄之间的关系"**（共享的世界 + 互相影响的方式）。

其余——**尤其"技能 / 招式 / 连招"这种概念——全是英雄内部自己的事，引擎不预设、不强加**（精神同 MUGEN：引擎管碰撞 / 伤害 / 世界，角色是自包含的自治体）。

两条由此长出的支柱：

- **英雄 = 一个协程**（不是回调对象）。启动时传入 context，之后每 tick 反复 resume；输入 / 事件都在 context 里。连招 = 顺序代码，协程挂起点就是状态。
- **状态二分**：**耐久 / 可见 / 要联网的状态 → ECS 组件；纯控制流 / 瞬时的状态 → 协程局部变量**。这一条同时解决快照、联网、热重载（见 [§5 设计准则](#5-设计准则)）。

---

## 2. 模块（细粒度，各独立库；heroes 为 DLL；client/server 为可执行）

| 模块          | 职责           | 关键内容                                                                                                                                                            | 三方（直接用）                       |
| ----------- | ------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------- |
| `graphics`  | 显示架构 / 渲染上下文 | 见 [render/renderable.md](render/renderable.md#bvn-渲染renderable设计-规范)，renderer env 二分见 [render/renderer.md](render/renderer.md#10-两个-env-renderer不再有合体-renderer) | Vulkan·vk-bootstrap imgui·stb |
| `platform`  | 系统层          | 窗口 / 输入 / 计时 / fs / **动态库加载**                                                                                                                                   | SDL3                          |
| `simulator` | 仿真核心         | 见 [simulator.md](simulator.md#bvn-仿真核心simulator)：**裸 registry 即 World**·tick/快照·**共享交互底座**（空间 / 碰撞 / effect 通道 / 属性）                                            | EnTT                          |
| `gameplay`  | MOBA 规则      | 比赛流程·经济·AI·**可选标准战斗约定**·plugin host·协程英雄 runtime                                                                                                                | —                             |
| `net`       | 网络           | `net_transport`·`input_command`·快照复制                                                                                                                            | ENet                          |
| `audio`     | 音频           |                                                                                                                                                                 | miniaudio                     |
| `assets`    | 资源           | 加载器·热重载·精灵表·Lua（M3 起）                                                                                                                                           | stb·sol2·lua                  |
| `heroes/*`  | 内容插件         | 每英雄一个**自包含**文件夹 → DLL                                                                                                                                           | 仅 sdk                         |
| `tools`     | 工具           | **英雄编辑器**·Tracy·调试覆盖层·replay                                                                                                                                    | imgui·Tracy                   |

工具链：**C++26**（clang-cl 优先 / MSVC 兜底；热重载构建用 MSVC）·**vcpkg**·**CMake**（每模块独立 CMakeLists + 顶层聚合 + CMakePresets）。

---

## 3. 目录结构

```
bvn/
  CMakeLists.txt cmake/  CMakePresets.json  vcpkg.json
  include/bvn/
    graphics/ platform/  simulator/ gameplay/ net/ audio/ assets/ tools/
        每个：include/bvn/<模块>/*.h（公共，外部 #include <bvn/<模块>/...>）+ source/（实现）
  heroes/                     # 自包含：每英雄一个文件夹
    kenpachi/{source, data, assets}   ichigo/{...}   ...
                             # 构建：每个 → 一个 DLL（+ 运行期拷 data/assets）
                             # 一个 mod = 一个自包含文件夹，丢进去即可
  apps/client/  apps/server/  # 客户端 / 无头服务器（server 不链 render/renderer/platform-window）
  assets/                     # 引擎级共享资源（大文件 gitignore）
  tests/                      # doctest（重点：sim 软确定性 / 回归）
```

---

## 4. 数据流（一帧）

1. **输入采集** →（联网时发给 host）。
2. **sim tick（30Hz 可配 · 固定流水线）**：
   - **决策阶段**：resume **每个英雄协程**（ctx 喂输入 / 事件 / 世界）→ 英雄读一致世界态、发 effect / 意图、发布可见状态。（所有英雄看到的是上一 tick 的结算态 + 本 tick 输入，互不看对方中途改动 → 公平 / 更确定）
   - **结算阶段**：引擎统一处理 移动 / 碰撞 / 标准战斗约定（伤害 / 死亡 / status）/ effect 通道分发。
   - 随机走种子 RNG、时间 = tick。
3. **快照**：双缓冲 capture（registry → 后缓冲；含本帧渲染任务）。
4. **渲染（render scheduler）**：前缓冲 + 插值 alpha → 各 renderable 的 `render(global)` 任务在 `on_frame(pool, buffer)` 处被放行，分别录进自己持有的 secondary command buffer → render workflow 的 `submit()` 按本帧 waiter 顺序用栈上临时 vector join secondary，并完成帧开闭、submit、present（细节见 [render/render-scheduler.md §2 submit() 与一帧的编排](render/render-scheduler.md#2-submit-与一帧的编排)）。
5. **网络**：host 序列化快照广播；client 预测 + 和解。
6. **重计算**：寻路 / 粒子 / 大量单位 → sender 丢 compute scheduler（CPU 现在 / CUDA 以后）。

---

## 5. 设计准则

- **三方库直接用**：第三方库可大大方方直接使用，不必藏进实现、不必套隔离接口——只要**别让库影响设计**（设计先行）。注：`renderer` / `scheduler` / `net_transport` 这几个抽象是为**后端可换**的设计目标（Vulkan→CUDA、netcode 演进）而留，不是为隔离库。
- **simulator解耦 / 可无头**：`simulator` 不依赖 `render`/`platform`/`renderer`；`apps/server` 不链它们。
- **软确定性纪律**：种子 RNG、禁墙钟（时间 = tick）、EnTT 稳定迭代、集中数值运算（为将来 lockstep 留门）。
- **状态二分**：耐久 / 可见 / 联网 → ECS 组件；纯控制流 / 瞬时 → 协程局部变量。
- **热重载 = 重启协程**：协程 + Live++ 有摩擦 → 热重载时取消旧协程、**从 ECS 当前态重启**新协程（瞬时态丢失可接受）。详见 [plugin/hot-reload.md §4 结论](plugin/hot-reload.md#4-结论)。
- **最大插件自由**：英雄是自治协程 + 直接 ECS；引擎不强加"技能"概念。
- **两个 CUDA 入口**：计算换 scheduler、渲染换 `renderer`；现在只预留，实现在 M8。
- **内存**：禁止裸`new`/`delete`，用智能指针，容器解决，必要时设计新容器。
- **错误处理**：可恢复的运行时错误：用**异常**；程序 Bug / 逻辑错误：用**断言/契约(c++26)**，不可恢复错误：炸程序。
