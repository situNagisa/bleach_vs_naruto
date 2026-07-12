# bvn 渲染·render scheduler（设计 + 规范）

> render scheduler 的职责、`submit()` 一帧编排、frame generation 与收尾契约；采用模型 B 并行录制 secondary command buffer。
> render task 侧形态见 [render-task.md](render-task.md#bvn-渲染render-task设计-规范)；启动 / 注册见 [boot.md](boot.md#bvn-渲染启动boot)。

---

## 1. scheduler

以下称呼指同一个对象：

| 称呼 | 场景 |
|---|---|
| render scheduler | 文档中的概念 |
| `render_workflow` | 实现该概念的类型 |

游戏主体提供两条独立 scheduler：

| scheduler | 跑什么 | 对外保证 |
|---|---|---|
| **main scheduler** | 每个 `entity.main`、entity 派生的逻辑任务 | —— |
| **render scheduler** | 所有 render task | 并行录制；绘制顺序未定义；`submit()` join |

render scheduler 对外提供三组操作：

| 调用方 | 操作 | 语义 |
|---|---|---|
| render task | `co_await render_workflow.async_record(pool)` | 挂起至下一次被放行；正常路径取得绑定 generation 的 frame，停止路径取得空 frame |
| render task | `frame.allocate()` / `frame.retire(command)` | 从 task 自有 pool 取得 command、预留 generation 条目，并在录制成功后发布与绑定回收 sender |
| 游戏主体 | `render_workflow.submit()` | 开帧、并行放行 task、join secondary、submit、present |
| 收尾编排 | `wait_for_submit()` / `complete_submissions()` / `request_stop()` / `finish()` | 排空 CPU submit、在 GPU 停止访问后完成 generation、恢复停靠 task、停止内部 pool |

不同 render task 可以并行录制。`frame.allocate()` 在当前 generation 中预留未发布条目；`frame.retire(command)` 的 operation start 在录制成功后原子发布 raw handle 并注册 completion callback。`submit()` 只收集已发布条目，其顺序由并发预留形成，绘制顺序未定义。线程、frame slot 与 generation 都由 render scheduler 管理，task 只处理本次拿到的 frame。

---

## 2. `submit()` 与一帧编排

### 2.1 主流程

游戏主体每帧调用一次 `submit()`。开 / 收 rendering、queue submit 与 present 均在该函数内联完成：

```cpp
auto render_workflow::submit() -> void
{
	::stdexec::sync_wait(_inner_scope.on_empty());
	// 取出上一轮异步编排保存的异常；有异常则重抛。

	// 开帧
	{
		// resize request
		// vkWaitForFences(current slot)
		// 完成该 slot 上一次 submission generation
		// vkAcquireNextImageKHR
		// reset fence / primary pool
		// vkBeginCommandBuffer(primary)
	}

	// 开 rendering
	{
		// layout barriers
		// vkCmdBeginRendering(primary, SECONDARY_COMMAND_BUFFERS_BIT)
	}

	auto frame_state = ::std::make_shared<render_frame_state>();
	auto waiters = /* 在 mutex 下复制 recovery 列表，再 swap _waiters */;
	// 给每个 waiter 写入同一个 frame_state、当前 slot/image 与它注册的 task pool。

	auto work =
		::stdexec::just(::std::move(waiters))
		| ::stdexec::continues_on(_recording_pool.get_scheduler())
		| ::stdexec::bulk(::stdexec::par, waiter_count, /* 并行 resume */)
		| ::stdexec::continues_on(get_scheduler())
		| ::stdexec::then([frame_state]
		{
			auto secondaries = /* 在 frame_state mutex 下收集已发布条目 */;
			::vkCmdExecuteCommands(primary, secondaries.size(), secondaries.data());

			// 收 rendering 与收帧
			{
				// vkCmdEndRendering / present layout barrier / vkEndCommandBuffer
				// vkQueueSubmit2 / 将 frame_state 绑定到 slot / vkQueuePresentKHR
				// rotate frame slot
			}
		});

	_inner_scope.spawn(::std::move(work));
}
```

开帧与 `vkCmdBeginRendering` 保持同步；task 录制与收帧部分由 `_inner_scope` 追踪。下一次 `submit()` 先等待该 scope 为空，因此不会与上一轮 CPU 侧 join / submit / present 交叠。

slot fence 与 swapchain image acquire 使用有限等待。超时时 `submit()` 直接结束本轮，不建立 generation、也不放行 waiter，使事件循环可以继续处理窗口关闭等平台事件。

### 2.2 generation 完成边界

每次放行 waiters 都建立独立的 frame generation。它在以下一个边界完成：

| 路径 | 完成时机 |
|---|---|
| queue submit 成功 | 再次复用对应 slot，且 `vkWaitForFences` 成功之后 |
| queue submit 前失败 | 当轮收帧异常路径立即完成 |
| 全局收尾 | GPU idle 后由 `complete_submissions()` 完成全部在途 generation |

queue submit 成功后，即使 present 失败，该 generation 仍由对应 fence 或全局 GPU idle 完成。这样 secondary command buffer 只会在 GPU 不再引用它之后回到 task 的 free list。

### 2.3 顺序

当前没有独立排序键。`submit()` 直接执行各 task 并发预留后形成的 secondary 集合；以后需要游戏层排序时，再引入明确排序键。

---

## 3. 帧录制与回收

### 3.1 `async_record(pool)`

awaitable 保存 task 自有 pool 的 observer，并把自身地址注册到 waiter 集合。注册与 `request_stop()` 在同一个 waiter mutex 下裁决：已停止时 `await_suspend()` 返回 `false`，task 立即取得空 frame；先注册成功时，停止态 `submit()` 必须恢复它。`submit()` 在正常恢复 waiter 前写入精确的 frame 结果；`await_resume()` 只返回该结果，不读取随后可能轮转的 workflow 当前槽。

```cpp
struct async_record_awaitable
{
	render_workflow* _workflow = nullptr;
	secondary_command_pool* _commands = nullptr;
	::std::coroutine_handle<> _parent;
	render_recording_frame _frame;

	static constexpr auto await_ready() noexcept { return false; }
	auto await_suspend(::std::coroutine_handle<> parent) -> bool;
	auto await_resume() noexcept -> render_recording_frame;
};
```

### 3.2 `allocate()`

每个 render task 持有一个 `secondary_command_pool`。pool 内用 `free_list` 管理动态数量的 secondary command buffer：有已完成的 buffer 就复用，没有就从同一个 Vulkan command pool 新分配。`render_recording_frame::allocate()` 完成两件事：

1. 从 task pool acquire 并 reset 一个 secondary command buffer；
2. 在本 generation 的 mutex 下预留一个未发布条目。

录制成功后，retirement operation start 在同一 mutex 下把条目标记为已发布并挂入 completion callback 链表。`submit()` 只执行已发布条目；录制或 spawn 失败时，lease 析构立即归还 free list，未发布条目被忽略。一个 task 每帧可以分配零个、一个或多个 command buffer。task 不保存 frame slot 数量，也不轮转 slot。

### 3.3 retirement sender

`frame.retire(command)` 返回 completion signatures 只有 `set_value_t()` 的 sender。`::stdexec::spawn` 成功启动 operation 时，command 才发布到本 generation 并绑定回收；generation 完成后 command index 回到 task pool 的 free list。sender 不响应 stop。

render task 用 `simple_counting_scope` 追踪所有 retirement；`close()` 先禁止新增 association，`join()` 再等待既有 retirement 完成。唯一完整代码形态见 [render-task.md §1](render-task.md#1-render-task-的形态)。

---

## 4. render scheduler 契约

1. **每帧流水**：`begin frame → begin rendering → 并行恢复 task → execute secondaries → end rendering → submit → present`。
2. **绘制顺序**：不同 render task 之间、同一 generation 的并发 push 之间均无顺序承诺。
3. **职责**：`submit()` 负责 rendering 开闭、join、submit 与 present；task 负责自己的 pool、buffer 与 draw 录制。
4. **frames-in-flight**：workflow 维护 frame slot 与 submission generation；task 每次只看到一个 `render_recording_frame`。
5. **资源发布与回收**：成功录制的 command lease 交给 `frame.retire()`，并由仍开放的 `simple_counting_scope` spawn；operation start 是发布点，generation 完成是回收点。
6. **生命周期**：严格遵守 [§5](#5-收尾次序)。

---

## 5. 收尾次序

render scheduler 收尾次序（硬约束）：

1. 收到全局结束请求后，主循环停止发起新的 `submit()`；调用 `wait_for_submit()` 等待当前 CPU 侧录制、join 与 queue submit 编排结束，使长期 render task 停靠在 `async_record(pool)`，协程帧保持存活。
2. 保持 render task、render workflow 和 renderer 的 Vulkan 资源存活，调用 `vkDeviceWaitIdle`，或等待全部在途 frame slot 的 fence；device loss 同样终止 GPU 对这些资源的访问。
3. GPU 排空或 device loss 后调用 `render_workflow.complete_submissions()`，完成全部在途 generation，使 retirement sender 可以归还 command。
4. 向 render task 的 env 传播 stop，调用 `request_stop()` 后再调用一次 `submit()`，在 render scheduler 上恢复所有停靠 task；task 退出循环后 `close()`、不可取消地 `join()` retirement scope，再析构协程局部资源。
5. 排空 render scope，再排空 main scope。
6. 调用 `finish()` 停止 workflow 内部 pool；析构 frame env 的资源 owner，最后析构 global env 的资源 owner 与 device。

第 2→3→4→6 步的次序不可调换：GPU 排空前 command 不得回收，retirement 未 join 前 task pool 不得析构，task 资源析构前 device 不得销毁。结束信号来源见 [render-task.md §2](render-task.md#2-结束信号恒从-env-取)。

---

## 6. 一帧数据流

1. task 持有一个 secondary command pool，在 `async_record(pool)` 挂起。
2. `submit()` 等上一轮 CPU 编排结束，等待当前 slot fence，并完成该 slot 的上一 generation。
3. `submit()` 开帧、建立新 generation、并行恢复本轮 waiters。
4. task 从自己的 free list acquire command 并预留 generation 条目；录制成功后，retirement operation start 发布 handle。
5. `submit()` 只 join 已发布 secondary，收帧并 queue submit；generation 绑定到实际提交使用的 slot。
6. 该 slot fence 完成后，所有 retirement sender 把 command 归还各自 task pool。
