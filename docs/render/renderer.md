# bvn 渲染·renderer（设计 + 规范）

> renderer 的设计、形态、职责，以及该如何使用（写给 render task 的示范）。
> 本文只讲**规范级**：renderer 是什么、持有什么、给 renderable 什么边界、一帧分几阶段。
> 具体 vulkan 命令序列、以及"为什么现在用 vulkan 充当 renderer 规范"见 [renderer-vulkan-impl.md](renderer-vulkan-impl.md#bvn-渲染renderer-的-vulkan-实现现状-实现)；vulkan 基础概念见 [vulkan-qa.md](vulkan-qa.md#bvn-vulkan-概念问答qa)。
> 帧的开闭由谁驱动见 [render-scheduler.md §2](render-scheduler.md#2-submit-与一帧的编排)；并发模型 A/B 见 [render-scheduler/model-ab.md](render-scheduler/model-ab.md#bvn-渲染并发模型-a-b设计)。

---

## 1. renderer 的形态与职责划分

### 1.0 renderer 是一个 concept，vulkan 是它的一份实现

renderer 不是一个具体类型，而是一组 **concept**：它约束「一个东西要能当 renderer，得能读出哪些渲染环境」，而不规定这些环境背后是谁。凭空设计一个足够优秀的 renderer 不可能（见 [renderer-vulkan-impl.md §1](renderer-vulkan-impl.md#1-现状初期用-vulkan后期萃取规范)），所以现阶段唯一的实现直接用 vulkan——但 render task 依赖的是 **concept**，不是那份 vulkan 实现，故 A↔B 切换、乃至将来换 CUDA renderer，内容侧无须改动。

concept 按 render task 的**两种依赖时机**二分：

| concept | 读出什么 | render task 何时用 |
|---|---|---|
| **global-env renderer** | 框架级、整程序不变的环境（instance / device / queue / swapchain 等） | 初始化期：建 pipeline / buffer / texture |
| **frame-env renderer** | 每帧变的绘制环境（**当帧交给本 task 的 command buffer** 等） | 录制期：把 draw 录进去 |

```
renderer = global-env renderer && frame-env renderer
```

render task 只依赖 `global-env && frame-env` 这个并集，不依赖任何具体类型。

#### 为什么二分 + 转发：submit() 按帧组装 renderer 递给每个 task

二分不是为了整洁，而是为了让 renderer **可组装**：一个完整 renderer = 一份全程不变的 global-env + 一份**每 task、每帧现攒**的 frame-env。`submit()` 放行某个 task 时，把「不变的 global-env」和「本帧本 task 专属的 frame-env」组装成一个完整 renderer 递给它——task 于是拿到的是「为它这一次录制量身拼好的 renderer」。frame-env 里最关键的一格就是 `command_buffer()`：模型 B 下每 task 各录各的 secondary，具体哪一条由这次组装填入，task 不再需要额外接一个索引参数来自己挑（见 [render-scheduler/model-ab.md §3](render-scheduler/model-ab.md#3-模型-bsecondary-command-buffer并行录制-控序-设计方向)）。

组装靠**转发实现**：一层薄壳，持有指向内层环境的指针，逐访问器转发到内层；要换某一格（如 `command_buffer()` 的来源）就继承薄壳、只改那一格，其余照转。转发壳因此既**传递轻**（只是指针，可按值传 renderer），又**可局部特化**（不必重写整份实现）。

**跨开发者 → 转发须是动态的**：各 entity 由不同开发者独立开发、分别编译，render task 落在编译边界之外。`submit()` 要把组装好的 renderer 从自己这边**传到编译边界另一侧的 task**，转发壳就不能是「编译期定死内层类型」的模板，而须是**动态转发**（对内层做类型擦除）：`submit()` 侧把当帧真实 renderer 塞进壳，task 侧只对着 concept 编程、调访问器即拿到真身的数据。这样 renderer 才能从 `submit()` 流到任意第三方 task。

### 1.1 初始化

游戏主体在合适时机初始化那份 vulkan 实现（满足 global-env + frame-env 两个 concept）。现阶段只有一个实现，所以不再抽象 `rhi` / `context` 层；实现自己持有 instance / device / swapchain 等框架级状态，经 global-env concept 读出。render task 直接读取这些 handle，并用底层 API 创建自己的 pipeline / buffer / texture 等 GPU 资源——怎么定 vertex 格式、要不要透明混合（2D sprite 的核心需求），全是它自己的事。

### 1.2 绘制期

render task 在 `render` 里录制自己的 draw 命令。它从 **frame-env** 读出当帧交给它的 command buffer（`command_buffer()`），只认这个 command buffer，不碰帧结构（begin / end rendering）与 submit / present。这个 command buffer 是 primary 还是 secondary、模型 B 下具体是哪一个 secondary，都由 frame-env 解析，task 不感知（见 [render-scheduler/model-ab.md](render-scheduler/model-ab.md#bvn-渲染并发模型-a-b设计)）。

### 1.3 renderer 持有什么

那份 vulkan 实现作为持久对象，管理所有框架级状态——instance / surface / device / queue、swapchain 及其 image / view、深度图、以及每个 in-flight 帧槽的命令池 / 命令缓冲 / 同步对象。完整清单见 [renderer-vulkan-impl.md §2 持久环境](renderer-vulkan-impl.md#2-持久环境renderer-一次性建立整程序持有)；这些数据经 global-env（框架级）与 frame-env（当帧级）两个 concept 读出，两个 concept 各要求哪些访问器见 [renderer-vulkan-impl.md §7](renderer-vulkan-impl.md#7-两个-concept-的访问器vulkan-实现)。

实现是一个**纯数据 context**：它只**持有**这些 handle（**全部 in-flight 帧槽也由它持有**），**不定义** begin / end frame / rendering 之类的帧生命周期函数——帧的开闭是 render scheduler 的 `submit()` 的职责（见 [render-scheduler.md §2](render-scheduler.md#2-submit-与一帧的编排)）。

### 1.4 renderer 给 renderable 的接口边界

renderer **不包装资源创建**，避免做成第二套底层 API：renderable 读取 renderer 里的数据、直接调用底层 API。renderer 不为 renderable 增加辅助函数层；现阶段只暴露框架级数据。renderer 也**不定义帧生命周期函数**——帧开闭是 render scheduler `submit()` 的活。

### 1.5 渲染目标边界（现阶段约定）

renderer 决定渲染目标（attachment）结构（现阶段：单 color attachment + 单 depth，dynamic rendering，无多 pass / MSAA）；renderable 在此框架内自由决定 pipeline 的其余所有状态。需要多 pass 效果（描边、后处理）时再扩展 renderer 暴露相应能力。

---

## 2. 帧生命周期（五阶段·规范级）

> 按「持久环境 → 每帧环境 → 录制 → 结束一帧 → 结束全部」五段描述。这里只列**规范级职责与约束**；每阶段的具体命令序列见 [renderer-vulkan-impl.md](renderer-vulkan-impl.md#bvn-渲染renderer-的-vulkan-实现现状-实现)。

贯穿全篇的**铁律**：

> command buffer 及其所属 command pool 是**外部同步**对象——**同一个 command buffer 不能被并发录制**；queue 的提交 / 呈现亦然。这条铁律单独决定了并发模型（[render-scheduler/model-ab.md](render-scheduler/model-ab.md#bvn-渲染并发模型-a-b设计)）。

### 2.1 持久环境（renderer 一次性建立，整程序持有）

renderable **只读不建**。renderer 持有框架级 + 每个 in-flight 帧槽的资源（含每帧槽一份深度图，避免跨帧清深度的 hazard）。**frames-in-flight 的含义**：让 CPU 录第 N+1 帧时 GPU 仍在跑第 N 帧；代价是任何「CPU 每帧写、GPU 每帧读」的资源须备 N 份。清单与命令见 [renderer-vulkan-impl.md §2](renderer-vulkan-impl.md#2-持久环境renderer-一次性建立整程序持有)；概念见 [vulkan-qa.md §1](vulkan-qa.md#1-inflight是什么为什么需要)。

### 2.2 每帧环境（开帧 + 开 rendering，在所有 task 之前）

由 render scheduler 的 `submit()` 在所有 task 之前执行：acquire、reset、开命令缓冲、布局转换、开 dynamic rendering。**契约：开 / 收 rendering 永远由 `submit()` 执行，render task 绝不自调**；task 只在已开启的 rendering 实例内录 draw。命令序列见 [renderer-vulkan-impl.md §3](renderer-vulkan-impl.md#3-每帧环境开帧-开-rendering在所有-task-之前)。

### 2.3 录制期（render task）

单个 task 的命令清单（首参皆 `cmd`）：bind pipeline / set viewport·scissor / bind vertex·index·descriptor / push constants / draw。这里的 `cmd` 就是 task 从 frame-env 读出的 `command_buffer()`——它是自己的 secondary（模型 B）还是共享的 primary（模型 A 基线），由 frame-env 解析，task 不感知（见 [render-scheduler/model-ab.md §4](render-scheduler/model-ab.md#4-对-render-task-透明)）。第三方渲染后端入口（如 ImGui backend）可由 renderable 直接调用，只要它同样只把 draw 录进这个 `cmd`，不接管帧结构 / submit / present。命令细节见 [renderer-vulkan-impl.md §4](renderer-vulkan-impl.md#4-录制期render-task)。

### 2.4 结束一帧（收 rendering + 收帧）

本帧全部 task 录完后，`submit()` 收这一帧：转呈现布局、收命令缓冲、提交、呈现、轮转帧槽。**submit 是每帧的 join 点**。命令序列见 [renderer-vulkan-impl.md §5](renderer-vulkan-impl.md#5-结束一帧收-rendering-收帧)。

### 2.5 结束全部（关机）

次序为硬约束（GPU 在用的不能毁、device 不能先于其资源毁）：

1. 排空 render scope（确保无 task 在录 / 在飞）
2. device wait idle
3. 各 renderable 析构 → 毁自建的 pipeline / buffer / image / descriptor pool / sampler / shader module
4. renderer 毁自有 per-frame / swapchain 资源
5. device → surface → debug messenger → instance

**第 1→3 步次序对协程架构是硬约束**：render scope 先排空、renderable 先析构，renderer 才能拆，否则 renderable 持有的 handle 在 device 销毁后变野指针。收尾在运行时怎么编排见 [render-scheduler.md §4 收尾次序](render-scheduler.md#4-收尾次序)。
