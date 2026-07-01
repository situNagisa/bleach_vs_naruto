# bvn 引擎架构定稿（Engine Spec v1）

> 日期：2026-06-15　状态：**定稿 · 架构唯一权威**（经"模块 / 目录 / 接口 / 数据流 / 里程碑"逐项问答确定）
> **架构冲突一律以本文为准；编码风格以 cpp-coding-standard 为准。**
> 接口为**草图**；标注"待定"的细节留到动手时再敲。

---

## 1. 核心哲学（一切的定调）

**引擎 = 平台，只干三件事：**
1. **渲染**英雄表达的东西；
2. 把**输入 / 交互**喂给英雄；
3. 处理**"英雄之间的关系"**（共享的世界 + 互相影响的方式）。

其余——**尤其"技能 / 招式 / 连招"这种概念——全是英雄内部自己的事，引擎不预设、不强加**（精神同 MUGEN：引擎管碰撞 / 伤害 / 世界，角色是自包含的自治体）。

两条由此长出的支柱：

- **英雄 = 一个协程**（不是回调对象）。启动时传入 context，之后每 tick 反复 resume；输入 / 事件都在 context 里。连招 = 顺序代码，协程挂起点就是状态。
- **状态二分**：**耐久 / 可见 / 要联网的状态 → ECS 组件；纯控制流 / 瞬时的状态 → 协程局部变量**。这一条同时解决快照、联网、热重载（见 §4.4 / §6）。

---

## 2. 模块（细粒度，各独立库；heroes 为 DLL；client/server 为可执行）

| 模块 | 职责 | 关键内容 | 三方（直接用） |
|---|---|---|---|
| `display_architecture` | 显示架构 | 见`display-architecture.md` |  |
| `platform` | 系统层 | 窗口 / 输入 / 计时 / fs / **动态库加载** | SDL3 |
| `renderer`（原`rhi`） | 渲染 | 设备 / 交换链 / 缓冲 / 纹理 / 管线 / 提交 | volk·VMA·vk-bootstrap |
| `render` | 表现 | 2D 精灵·相机·**渲染任务执行**·HUD | imgui·stb |
| `simulator`（原`sim`） | 仿真核心 | **裸 registry 即 World**·tick/快照·**共享交互底座**（空间 / 碰撞 / effect 通道 / 属性） | EnTT |
| `gameplay` | MOBA 规则 | 比赛流程·经济·AI·**可选标准战斗约定**·plugin host·协程英雄 runtime | — |
| `net` | 网络 | `net_transport`·`input_command`·快照复制 | ENet |
| `audio` | 音频 |  | miniaudio |
| `assets` | 资源 | 加载器·热重载·精灵表·Lua（M3 起） | stb·sol2·lua |
| `heroes/*` | 内容插件 | 每英雄一个**自包含**文件夹 → DLL | 仅 sdk |
| `tools` | 工具 | **英雄编辑器**·Tracy·调试覆盖层·replay | imgui·Tracy |

工具链：**C++26**（clang-cl 优先 / MSVC 兜底；热重载构建用 MSVC）·**vcpkg**·**CMake**（每模块独立 CMakeLists + 顶层聚合 + CMakePresets）。

---

## 3. 目录结构

```
bvn/
  CMakeLists.txt cmake/  CMakePresets.json  vcpkg.json
  include/bvn/
    display_architecture/ platform/  renderer/  render/  simulator/  gameplay/  net/  audio/  assets/  tools/
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

## 4. 关键接口与契约

### 4.1 sim：裸 registry 即 World
- **没有 World 包装类**——`::entt::registry` 就是世界，所有人直接读写。
- `tick` / `snapshot` 是**自由函数**：`simulator::tick(reg, inputs)`、`simulator::capture(reg, back)`。
- **全局态**（tick 计数 / 种子 RNG / 比赛状态）放 `reg.ctx()`（EnTT 上下文变量）。
- 实体句柄 = `::entt::entity`。组件：**通用组件在 simulator**（transform/velocity/…），**玩法组件在 gameplay**（经济 / 队伍 / 标准战斗约定的 Health 等）。
- 系统流水线**由引擎固定排序**（确定、好调试）；插件不直接插系统，通过协程接入。

### 4.2 共享交互底座（simulator，"通用机制"）
引擎提供、英雄按需使用的**原语**（这就是"处理英雄之间的关系"）：
- **空间查询 / 碰撞重叠检测**（英雄决定何时怎么用——怎么移动、何时生成判定框）。
- **通用 effect / 事件通道**（一个实体对另一个施加影响的统一渠道，不预设语义）。
- **灵活属性表**（见 §4.6 三层落地）。

### 4.3 可选标准战斗约定（gameplay，"可选不强制"）
一套标准 `health / damage / teams / death / status / 力` 的类型化组件 + 处理系统。
- 英雄**自愿**用：用的省事、引擎也能渲染血条等通用 UI；不用的走 §4.2 通用机制自定义。

### 4.4 英雄 = 协程（王牌）⭐
插件导出一个**工厂函数**产出英雄协程；引擎每 tick **resume** 它。无 onTick/onInput/onDamaged 回调——输入 / 事件 / 世界 / 渲染 API 全在 `ctx` 里、每次 resume 刷新。

```cpp
// 一个英雄 = 一个协程（示意）
hero_task kenpachi(hero_context context) {								   // 启动时传入 ctx
	auto combo = 0;                              						   // 瞬时态 = 协程局部变量（挂起自动保留）
    context.render_scope.spawn(::stdexec::starts_on(context.render_gate.scheduler_value, render(kenpachi_view, context.renderer)));	// 启动 render(renderer) 任务
	while (context.alive()) {
		auto&& in = ctx.input;
		co_await context.next_tick();									 // 挂起，将恢复点加入到英雄调度器当中，等待下一次调度
		if (in.pressed(key::j)) {										 // 连招 = 顺序代码，挂起点即帧推进
			++combo;
		}
	}
}
BVN_REGISTER_HERO(kenpachi, "kenpachi");
```
- **耐久 / 可见 / 联网态**（HP / 位置 / 当前帧 / 护盾）**必须写 ECS 组件**——快照只抓 registry，客户端不跑 B 的协程、只从快照渲染 B。
- 英雄协程靠不断把自己append到调度器上来不断刷新自己，实现update的效果（代替了回调），期间可以通过spawn启动别的任务。
- **`hero_context` 具体装什么 = 待定**（边界 = 那三件事 + 世界访问，不含任何"技能"概念）。

### 4.5 渲染表达：`renderable + render(renderer)`
- context 提供 render scheduler 与唯一 renderer；英雄或英雄管理的对象实现 `render(renderer)`，该函数本身就是渲染任务（sender / 协程）。
- `render(renderer)` 的参数只有 renderer；调度器、结束信号从协程环境取。渲染任务读取已经发布的快照状态，在 render scheduler 唤醒后只向 renderer 交给它的 command buffer 录制。
- **后端切换点 `renderer`**：现在用 Vulkan 实现 renderer；将来 CUDA 计算也只能作为 renderer / compute 的后端演进。renderer 是纯数据 context（持有 vulkan handle）；帧开闭、submit、present 归 render scheduler 的 `submit()`（见 `render-runtime.md §5`），呈现落在其内联的 Vulkan 帧命令上。

### 4.6 灵活属性表（三层落地）
- **标准约定属性**（health/mana/…）→ gameplay 类型化组件（快、标准系统与 UI 直接用）。
- **英雄私有 bespoke 状态** → **协程局部变量 / ECS 组件**（瞬时进协程，耐久进组件）。
- **需被别的实体 / 通用 effect 通道读写的非标准属性** → 通用 `Attributes` 组件（interned-id→值），**少量使用**。

### 4.7 计算 / 网络
- `::std::execution`：重活（寻路 / 粒子 / 大量单位 SoA）作为 sender 提交；CPU（`inline_scheduler`） 现在 → **nvexec CUDA scheduler** 以后。
- `net_transport`（ENet 后端）：`input_command{moveDir,aim,abilityBits,seq,tick}`；**host 权威 + 快照 + 移动预测 + 和解**；快照演进 全量 → delta → 兴趣区(AOI，兼作迷雾地基)；演进到专用服务器走同一无头 sim。

### 4.8 插件加载 / 可选辅助
- 开局**扫描 `heroes/` / `plugins/`**（自包含文件夹 → DLL）+ 每插件 **manifest**（id / 版本 / 依赖 / 资源）+ **ABI 整数版本校验**（不匹配拒载 + 日志）。
- **可选辅助库（不进引擎核心）**：招式帧数据时间线 / 动画状态机 / 逐帧 hit/hurt 框 + **Fighter Factory 式编辑器**——BvN 式连招英雄拿来用，不想用的绕开。
- 万物皆插件：装备 / 地图 / 模式同构（共享加载基建，接口各异），**分阶段实现**（M3 装备起）。

---

## 5. 数据流（一帧）

1. **输入采集** →（联网时发给 host）。
2. **sim tick（30Hz 可配 · 固定流水线）**：
   - **决策阶段**：resume **每个英雄协程**（ctx 喂输入 / 事件 / 世界）→ 英雄读一致世界态、发 effect / 意图、发布可见状态。（所有英雄看到的是上一 tick 的结算态 + 本 tick 输入，互不看对方中途改动 → 公平 / 更确定）
   - **结算阶段**：引擎统一处理 移动 / 碰撞 / 标准战斗约定（伤害 / 死亡 / status）/ effect 通道分发。
   - 随机走种子 RNG、时间 = tick。
3. **快照**：双缓冲 capture（registry → 后缓冲；含本帧渲染任务）。
4. **渲染（render scheduler）**：前缓冲 + 插值 alpha → 各 renderable 的 `render(renderer)` 任务按 render scheduler 顺序录制 → render scheduler 的 `submit()` 完成 Vulkan 帧开闭、submit、present。
5. **网络**：host 序列化快照广播；client 预测 + 和解。
6. **重计算**：寻路 / 粒子 / 大量单位 → sender 丢 compute scheduler（CPU 现在 / CUDA 以后）。

---

## 6. 设计准则

- **三方库直接用**：第三方库可大大方方直接使用，不必藏进实现、不必套隔离接口——只要**别让库影响设计**（设计先行）。注：`renderer` / `scheduler` / `net_transport` 这几个抽象是为**后端可换**的设计目标（Vulkan→CUDA、netcode 演进）而留，不是为隔离库。
- **simulator解耦 / 可无头**：`simulator` 不依赖 `render`/`platform`/`renderer`；`apps/server` 不链它们。
- **软确定性纪律**：种子 RNG、禁墙钟（时间 = tick）、EnTT 稳定迭代、集中数值运算（为将来 lockstep 留门）。
- **状态二分**：耐久 / 可见 / 联网 → ECS 组件；纯控制流 / 瞬时 → 协程局部变量。
- **热重载 = 重启协程**：协程 + Live++ 有摩擦 → 热重载时取消旧协程、**从 ECS 当前态重启**新协程（瞬时态丢失可接受）。
- **最大插件自由**：英雄是自治协程 + 直接 ECS；引擎不强加"技能"概念。
- **两个 CUDA 入口**：计算换 scheduler、渲染换 `renderer`；现在只预留，实现在 M8。
- **内存**：禁止裸`new`/`delete`，用智能指针，容器解决，必要时设计新容器。
- **错误处理**：可恢复的运行时错误：用**异常**；程序 Bug / 逻辑错误：用**断言/契约(c++26)**，不可恢复错误：炸程序。

---

## 7. 里程碑 → 模块映射

| M | 目标 | 落地重点 |
|---|---|---|
| **M0 · 地基** | 能跑的循环 + 三角形 | CMake/vcpkg/Presets · platform(SDL3) · renderer(Vulkan 三角) · **定步长循环 + 双缓冲骨架** · `::stdexec` · ImGui |
| **M1 · 渲染管线** | 看到 BvN 精灵在场景里 | `renderer`+`vulkan`· 3D 场景 + **2D billboard + 朝向翻转** · 侧俯视相机 · 精灵动画基础 · 资源加载+热重载 · 插值 |
| **M2 · 仿真 + 协程英雄 + 操控** | 一个英雄你能操控着跑 | 裸 registry/ECS + 固定流水线 · **plugin host + 加载英雄 DLL + 协程 runtime + ctx(输入)** · 移动 · **渲染任务路径打通** |
| **M3 · 战斗 + 连招** | **首个可玩：空场地英雄能跑能连招** | 共享交互底座 + 可选标准战斗约定 · 一套连招打木桩 · 瞄准指示器 · 打击感钩子 · **Lua/数据热重载 · 编辑器起步 · Live++** |
| **M4 · MOBA 生态** | 单线小局 | 小兵 / 塔 / 野怪 / 经济(等级/装备) / 寻路 / bot（先 1 路） |
| **M5 · 完整单机一局** | **首个惊艳里程碑** | 三线 + 野区 · 推基地 · 3 个 MVP 英雄(协程插件) · HUD/小地图/计分板 · bot 补满 5v5 |
| **M6 · 联网 MVP** | 两端联机 | `net_transport`+ENet · host 权威快照 + 移动预测 · 房间 + bot 补位 · 无头服务器雏形 |
| **M7 · netcode 加固** | 公网可玩 | 快照 delta/AOI · 和解打磨 · 专用服务器 |
| **M8 · CUDA 计算** | 引擎炫技 | nvexec scheduler + interop · 为自定义 CUDA 渲染铺路 |
| **M9 · 内容与表现** | 量产 | 编辑器量产真英雄 · 装备 · 音效 · 世界观 · 打磨 |

> 优先级：**M0–M1 先打通引擎 / 渲染 → M2 落地协程英雄 + 插件系统 → M3 首个可玩 → M5 完整一局**，再碰联网（M6+）与 CUDA（M8）。
