# bvn 渲染·启动（boot）

> 状态：设计 + 规范。
> 渲染侧「怎么被跑起来」：游戏主体只提供运行环境，entity 在自己的 `main` 里启动 render task；游戏主体不认识 entity 内部。
> 关联：render task 形态见 [render-task.md §1 render task 的形态](render-task.md#1-render-task-的形态)；render scheduler 与 `submit()` 见 [render-scheduler.md §2](render-scheduler.md#2-submit-与一帧编排)；client 现状（entity 都放哪）见 [../client/impl.md §1](../client/impl.md#1-entity-都放在-client-下)。

---

## 1. 核心哲学：游戏主体只提供运行环境

游戏主体（`client` 可执行）**不认识**英雄、UI、输入是什么，它只做两件事：

1. 把每个 **entity** 跑起来——在 main scheduler 上启动 `entity.main`；
2. 给 entity 提供**设施**——两条 scheduler（main / render）、renderer、程序结束信号——并**每帧驱动 render scheduler 出一帧**。

entity 想做什么、想画什么，全在它自己的 `main` 里完成。游戏主体永远不替 entity 写「该怎么画英雄」「该怎么画 UI」这类逻辑。

> **entity 是什么**：任何东西。英雄、游戏 UI、输入处理、调试覆盖层、场地——都是 entity。它们彼此独立、互不知道对方存在。（当前 client 里有哪些 entity、它们代表什么，见 [../client/impl.md §1](../client/impl.md#1-entity-都放在-client-下)。）

---

## 2. 职责二分：游戏主体 vs entity 实现者

### 2.1 游戏主体的责任（只此而已）

- **持有设施**：窗口、renderer、裸 `::entt::registry` 世界、两条 scheduler、程序结束信号。
- **启动 entity**：对每个 entity，在 main scheduler 上 spawn 它的 `entity.main`。
- **每帧驱动渲染**：每帧调用 render scheduler 的 `submit()`（见 [render-scheduler.md §2](render-scheduler.md#2-submit-与一帧编排)）。这是游戏主体在渲染侧唯一主动做的事——它**不**往 render scheduler 上放任何自己的 task：帧的开闭已经在 `submit()` 内部，不再需要一个单独的「帧任务」。
- **收尾**：按 [render-scheduler.md §5](render-scheduler.md#5-收尾次序) 完成渲染与 scheduler 清理，随后退出程序。

游戏主体**只能见到 entity 的一个接口：`main`**。它不知道 entity 内部有没有英雄状态机、有几个渲染任务、画的是 sprite 还是 mesh。

```cpp
int main()
{
	spawn(main scheduler, entity1.main(game context));
	spawn(main scheduler, entity2.main(game context));
	return 0;
}
```

### 2.2 entity 实现者的责任

- 在 `entity.main` 里做自己需要的一切：读输入、推自己的状态、`spawn` 自己的 render task。
- 自己管理自己的资源、自己的并发、自己的绘制顺序内部细节。

### 2.3 这条边界划掉了什么

游戏主体**不**为 entity 提供：

- 「渲染英雄 / UI 的辅助函数」——怎么画是 entity 自己的事；
- 跨 entity 的「可绘类型注册表」「draw 抽象」「场景图」——核心不建模场景（见 [renderable.md §2](renderable.md#2-核心契约)）；
- entity 之间的中间层——它们相互独立，不需要被抽象到一起。

> 一句话：**entity 的实现代码不进游戏主体**。如果你发现自己在游戏主体里写「针对某种 entity」的代码，那是放错了地方。

---

## 3. entity 怎么启动 render task

在 `entity.main`（跑在 main scheduler 上）里，entity 把所需 render task 关联到 render scope，并从 `render_workflow.get_scheduler()` 取得 scheduler value。启动就这一步，是 entity 唯一的渲染侧动作；对外它仍然只暴露 `main`。不同 render task 的绘制顺序未定义（见 [render-scheduler.md §2.3](render-scheduler.md#23-顺序)）。

```cpp
auto entity::main(context& game_context) -> task
{
	game_context.render_scope.spawn(
		::stdexec::starts_on(
			game_context.render_workflow.get_scheduler(),
			::bvn::graphics::render(
				*this,
				::bvn::graphics::dynamic_forward_global_env_renderer(
					game_context.renderer.global_env()
				)
			)
		)
	);
	co_return;
}

auto entity::render(global_renderer global) -> task;
```
