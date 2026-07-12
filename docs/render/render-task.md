# bvn 渲染·render task（设计 + 规范）

> render task 的协程形态、secondary command 所有权与结束契约。
> renderable 概念见 [renderable.md](renderable.md#bvn-渲染renderable设计-规范)；一帧编排见 [render-scheduler.md §2](render-scheduler.md#2-submit-与一帧编排)；renderer 上下文见 [renderer.md §1](renderer.md#1-renderer-的形态与职责划分)。

---

## 1. render task 的形态

以下称呼指同一个运行实体：

| 称呼 | 场景 |
|---|---|
| render task | 文档中的概念 |
| `graphics::render(...)` 返回的协程 / sender | 代码中的实体 |

render task 启动一次，在协程帧内完成：

1. 初始化 pipeline、buffer、texture 与一个 `secondary_command_pool`；
2. 循环通过 `async_record(pool)` 等待下一帧；
3. 每帧按需分配零个、一个或多个 secondary command buffer 并录制 draw；
4. 用 `::stdexec::spawn` 把每个 command 的回收绑定到本帧 generation；
5. 结束时关闭并 join retirement scope，随后由 RAII 析构资源。

`render(...)` 的唯一 renderer 参数是 global env renderer。每帧变化的数据和 recording generation 由 `async_record(pool)` 返回：

```cpp
auto T::render(::bvn::graphics::global_dynamic_forward_env_renderer global)
	-> ::bvn::gameplay::task
{
	auto env = co_await ::nagisa::concurrency::environment();
	auto stop = ::stdexec::get_stop_token(env);
	auto scheduler = ::stdexec::get_scheduler(env);

	auto pipeline = make_pipeline(global);
	auto vertices = make_buffer(global);
	auto commands = ::bvn::graphics::secondary_command_pool{
		global.device(),
		global.graphics_queue_family(),
	};
	auto retirements = ::stdexec::simple_counting_scope{};
	auto render_error = ::std::exception_ptr{};

	try
	{
		while (!stop.stop_requested())
		{
			auto frame = co_await render_workflow->async_record(commands);
			if (!frame || stop.stop_requested())
				break;

			auto command = frame.allocate();
			auto raw_command = command.get();
			auto color_format = global.swapchain_image_format();
			auto begin_info = ...;

			if(::vkBeginCommandBuffer(raw_command, &begin_info)) throw;
			::vkCmdBindPipeline(raw_command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);
			::vkCmdBindVertexBuffers(raw_command, /* ... */);
			::vkCmdDraw(raw_command, /* ... */);
			if (::vkEndCommandBuffer(raw_command) != ::VK_SUCCESS) throw;

			::stdexec::spawn(
				frame.retire(::std::move(command)),
				retirements.get_token()
			);
		}
	}
	catch (...)
	{
		render_error = ::std::current_exception();
	}

	retirements.close();
	co_await ::stdexec::unstoppable(
		::stdexec::starts_on(scheduler, retirements.join())
	);
	if (render_error)
	{
		::std::rethrow_exception(render_error);
	}
}
```

### 1.1 所有权与帧槽

| 对象                                   | owner                                  | 生命周期 / 职责                                         |
| ------------------------------------ | -------------------------------------- | ------------------------------------------------- |
| secondary Vulkan command pool        | render task                            | 整段 render task 生命周期                               |
| 从该 pool 分配的 secondary command buffer | render task 的 `secondary_command_pool` | 动态增长，由 free list 复用                               |
| command lease                        | 当轮 task 局部值，随后移交 retirement sender     | 从 acquire 到 generation 完成                         |
| primary frame slot Vulkan 资源         | `vulkan_context`                       | primary pool / command、fence、semaphore 与 depth 资源 |
| frame slot 选择 / 轮转与 generation state | render workflow                        | slot 编排、secondary 集合与完成通知                         |
| `render_recording_frame`             | task 的当轮局部值                            | 观察当前 generation 与 task pool                       |

task 不保存 frame slot 数量，也不自行轮转槽号。若上一批 command 仍在 GPU 上，pool 就分配新 buffer；对应 generation 完成后，retirement sender 把旧 index 放回 free list。动态 task 数量只影响各 task 自己实际分配的 buffer 数量。

### 1.2 每帧约束

- `async_record(pool)` 每轮返回一个精确 generation；停止恢复返回空 frame，task 必须在访问 frame 前检查 `!frame` 或 stop token。
- `frame.allocate()` 完成 acquire / reset，并在 generation 中预留一个未发布条目。
- `vkEndCommandBuffer` 成功后，task 把 lease 交给 `frame.retire()`，并在仍开放的 `simple_counting_scope` 上 spawn。
- retirement operation start 原子地发布 raw handle，并注册 completion callback；`submit()` 只执行已发布条目。
- 录制或 spawn 失败时，尚未发布的 lease 由析构立即归还 free list，对应条目不会进入 `vkCmdExecuteCommands`。
- 一个 task 每帧可调用 `allocate()` 任意次数，也可以不产生 command。

### 1.3 自定义形态

- **一次性绘制**：task 可以只等待和录制一次，随后 close / join retirement scope 并退出。
- **多个 render task**：一个 entity 可以注册多个 render task；绘制粒度落在 task 上。
- **实现位置**：`render` 可以由 entity 或 entity 管理的其他对象实现。

注册路径见 [boot.md §3](boot.md#3-entity-怎么启动-render-task)。

---

## 2. 结束信号：恒从 env 取

结束信号通过协程 env 的 stop token 传递：

- task 用 `stdexec::get_stop_token(env)` 取得 token，循环条件为 `!stop.stop_requested()`；
- 全局收尾按 [render-scheduler.md §5](render-scheduler.md#5-收尾次序) 先排空 GPU、完成在途 generation，再传播 stop 并恢复停靠 task；
- task 观察 stop 后退出循环，先 `close()` retirement scope，再以 `unstoppable(starts_on(scheduler, join()))` 等待全部 command 回收；
- join 完成后，secondary pool 与其余协程局部 RAII 资源才可析构。

异常路径使用同一收尾段：先保存 `exception_ptr`，close / join 后再重抛，保证异常不会越过 command 生命周期收尾。

---

## 3. render context 可 dump（未来向）

`render` 依赖的 context 应可 dump，也可从 dump 对象重建，用于快照、联网与离线恢复。现阶段只保留该方向。
