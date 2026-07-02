# bvn 客户端现状（client·impl）

> 只讲**当前实现**：client 里现在跑着哪些 entity、它们各自代表什么。
> 渲染侧「怎么被启动」的机制见 ../render/boot.md（本文只讲现状，不讲机制）。

---

## 1. entity 都放在 client 下

现阶段所有 entity 直接放在 `client` 下。这些 entity **模拟不同独立开发者各自的产出**——它们彼此独立、互不知道对方存在，正是 ../render/boot.md 里「游戏主体只提供环境、entity 自包含」那条边界的实地演练。

当前的 `arena` / `hero` / `debug_overlay` 就是三个这样的 entity，分别代表三个互不相干的开发者各自的成果。它们各自在自己的 `main` 里读输入、推状态、注册 render task；client 只负责把它们的 `main` 跑起来、每帧驱动 `submit()`。

<!-- TODO(补全): 待 client 目录代码稳定后，逐个补充各 entity 现在实现到什么程度（arena / hero / debug_overlay 各自现状）。当前仅记录结构约定。 -->
