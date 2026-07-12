# bvn 网络（net）

> 讨论较少的 theme：高信息密度讲清思路即可。承接 [engine-spec.md](engine-spec.md#bvn-引擎架构定稿engine-spec-v1) `net` 模块 + [decisions.md §7 网络](decisions.md#7-网络t7-t8)。

---

## 1. 思路

- **模块**：`net`，含 `net_transport`（ENet 后端）/ `input_command` / 快照复制。sim 不依赖 net；`apps/server` 走同一无头 sim。
- **模型**：**host 权威 + 快照 + 移动预测 + 和解**。`input_command{moveDir, aim, abilityBits, seq, tick}`。
- **快照演进**：全量 → delta → 兴趣区（AOI，兼作迷雾地基）。
- **确定性前提**：sim 软确定性（种子 RNG、时间 = tick、EnTT 稳定迭代、集中数值）为 lockstep 留门；键盘输入流天然适合 lockstep。

---

## 2. 计划 / 阶段

- 联网整体排在 M6+，**M5 前不碰**（先一路打通单机）。
- M6 联网 MVP：host 权威快照 + 移动预测 + 房间 + bot 补位 + 无头服务器雏形。
- M7 加固：快照 delta / AOI + 和解打磨 + 专用服务器。
- 里程碑全表见 [roadmap.md](roadmap.md#bvn-路线图roadmap)。
