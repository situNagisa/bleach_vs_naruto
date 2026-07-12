# bvn 仿真核心（simulator）

> 状态：**规范草图**（接口为草图，标注"待定"的细节留到动手时再敲）。
> 本文讲 simulator 模块的契约：裸 registry 即 World、共享交互底座、灵活属性表。模块定位见 [engine-spec.md §2 模块](engine-spec.md#2-模块细粒度各独立库heroes-为-dllclientserver-为可执行)，决策取舍见 [decisions.md §3 仿真与确定性](decisions.md#3-仿真与确定性t3)。

---

## 1. 裸 registry 即 World

- **没有 World 包装类**——`::entt::registry` 就是世界，所有人直接读写。
- `tick` / `snapshot` 是**自由函数**：`simulator::tick(reg, inputs)`、`simulator::capture(reg, back)`。
- **全局态**（tick 计数 / 种子 RNG / 比赛状态）放 `reg.ctx()`（EnTT 上下文变量）。
- 实体句柄 = `::entt::entity`。组件：**通用组件在 simulator**（transform/velocity/…），**玩法组件在 gameplay**（经济 / 队伍 / 标准战斗约定的 Health 等）。
- 系统流水线**由引擎固定排序**（确定、好调试）；插件不直接插系统，通过协程接入。

## 2. 共享交互底座（"通用机制"）

引擎提供、英雄按需使用的**原语**（这就是 [engine-spec.md §1 核心哲学](engine-spec.md#1-核心哲学一切的定调) 里"处理英雄之间的关系"）：

- **空间查询 / 碰撞重叠检测**（英雄决定何时怎么用——怎么移动、何时生成判定框）。
- **通用 effect / 事件通道**（一个实体对另一个施加影响的统一渠道，不预设语义）。
- **灵活属性表**（见 [§3 三层落地](#3-灵活属性表三层落地)）。

## 3. 灵活属性表（三层落地）

- **标准约定属性**（health/mana/…）→ gameplay 类型化组件（快、标准系统与 UI 直接用）。
- **英雄私有 bespoke 状态** → **协程局部变量 / ECS 组件**（瞬时进协程，耐久进组件）。
- **需被别的实体 / 通用 effect 通道读写的非标准属性** → 通用 `Attributes` 组件（interned-id→值），**少量使用**。
