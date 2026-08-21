结论：stdexec 里没有一对对称的 `as_sender/as_awaitable` 函数。它通过两套协议完成互操作：

| 方向 | 主要入口 | 核心实现 |
|---|---|---|
| sender → awaitable | `as_awaitable`、promise 的 `await_transform` | [__as_awaitable.hpp](D:/project/stdexec/include/stdexec/__detail/__as_awaitable.hpp:47) |
| awaitable → sender | `sender`、`get_completion_signatures`、`connect` 的 awaitable fallback | [__connect_awaitable.hpp](D:/project/stdexec/include/stdexec/__detail/__connect_awaitable.hpp:378) |

基于本地 `main@633c873`。

### sender → awaitable

典型调用链是：

```text
co_await sender
  -> promise.await_transform(sender)
  -> stdexec::as_awaitable(sender, promise)
  -> transform_sender(sender, get_env(promise))
  -> __sender_awaiter
  -> connect(sender, receiver)
  -> await_suspend(): start(operation_state)
```

`with_awaitable_senders<Promise>` 提供通用 `await_transform`，内部直接调用 `as_awaitable`，见 [__with_awaitable_senders.hpp](D:/project/stdexec/include/stdexec/__detail/__with_awaitable_senders.hpp:70)。

`as_awaitable` 的选择顺序是：

1. 对象自身的 `x.as_awaitable(promise)`。
2. `transform_sender` 后对象的 `.as_awaitable(promise)`。
3. 如果已经是普通 awaitable，原样返回。
4. 否则如果是兼容当前 promise 环境的 sender，构造 `__sender_awaiter`。
5. 最后是诊断或 identity fallback。

具体分派在 [__as_awaitable.hpp](D:/project/stdexec/include/stdexec/__detail/__as_awaitable.hpp:393)。

通用 `__sender_awaiter` 的结构大致是：

```text
coroutine frame
└── __sender_awaiter
    ├── value | exception_ptr | valueless(stopped)
    └── connect_result_t
        └── receiver 持有指向 awaiter 的引用
```

完成信号映射如下：

| sender 完成 | `co_await` 结果 |
|---|---|
| `set_value()` | `void` |
| `set_value(T)` | `decay_t<T>` |
| `set_value(Ts...)` | `tuple<decay_t<Ts>...>` |
| `set_error(exception_ptr)` | 重新抛出 |
| `set_error(error_code)` | 抛出 `system_error` |
| 其他 `set_error(E)` | 通过 `make_exception_ptr(E)` 抛出 |
| `set_stopped()` | 调用 promise 的 `unhandled_stopped()`，不会继续执行 `co_await` 后面的代码 |

值和错误存放在 variant 中；variant 保持 valueless 表示 stopped，见 [__as_awaitable.hpp](D:/project/stdexec/include/stdexec/__detail/__as_awaitable.hpp:104) 和 receiver 映射 [同文件](D:/project/stdexec/include/stdexec/__detail/__as_awaitable.hpp:176)。

这里要求至多一个 `set_value` 完成分支。多个不同成功分支需要先用 `into_variant` 收敛成一个分支。

异步 sender 还有一段很重要的竞争处理：

- 同线程内联完成时，receiver 不直接 `resume()`，而是清空原子 thread-id，让 `await_suspend()` 返回 continuation。
- 跨线程完成时，receiver 等 `await_suspend()` 完成对 coroutine frame 的最后一次访问后再恢复 coroutine。
- 这样同时避免了 use-after-free、内联完成造成的递归栈溢出，以及 stopped 内联完成的死锁。

这段握手位于 [__as_awaitable.hpp](D:/project/stdexec/include/stdexec/__detail/__as_awaitable.hpp:151)。静态确定“一定内联完成”的 sender 有单独的无原子优化路径，[同文件](D:/project/stdexec/include/stdexec/__detail/__as_awaitable.hpp:353)。

### awaitable → sender

这个方向没有显式包装函数，而是把符合条件的 awaitable 直接视为 sender。

首先，`sender` 的默认启用条件包含：

```cpp
has sender_concept
    || awaitable<S, stdexec-synthetic-promise>
```

见 [__sender_concepts.hpp](D:/project/stdexec/include/stdexec/__detail/__sender_concepts.hpp:59)。因此 raw awaiter、`operator co_await` 类型、提供 `.as_awaitable(promise)` 的类型，都可能自动满足 `sender`。

然后自动合成 completion signatures：

```cpp
completion_signatures<
  set_value_t(await_resume_result), // void 时为 set_value_t()
  set_error_t(std::exception_ptr),
  set_stopped_t()
>
```

见 [__get_completion_signatures.hpp](D:/project/stdexec/include/stdexec/__detail/__get_completion_signatures.hpp:100)。

最后 `connect(sender, receiver)` 的分派顺序是：

1. `S::__static_connect(...)`
2. `sender.connect(receiver)`
3. awaitable fallback
4. 旧的 `tag_invoke`

见 [__connect.hpp](D:/project/stdexec/include/stdexec/__detail/__connect.hpp:256)。第三步会调用 `__connect_awaitable`。

其 operation state 内含：

```text
__connect_await::__opstate
├── receiver
├── 原始 awaitable / 转换后的 awaitable / awaiter
└── synthetic coroutine frame
      resume -> __opstate::__on_resume()
```

`start()` 手动模拟标准 `co_await` 协议：

```text
promise.await_transform(x)
operator co_await(x) 或 identity
await_ready()
await_suspend(synthetic_handle)
await_resume()
```

然后映射为：

- `await_resume()` 返回值 → `set_value`
- awaiter 构造、`await_ready/suspend/resume` 抛异常 → `set_error(exception_ptr)`
- `promise.unhandled_stopped()` → `set_stopped`

核心代码在 [__connect_awaitable.hpp](D:/project/stdexec/include/stdexec/__detail/__connect_awaitable.hpp:397)。

这里的 synthetic frame 是当前实现最巧妙的部分：它把恢复函数指针直接放进 operation state，用伪 coroutine handle 接收 awaiter 的恢复，不需要创建一个真正的辅助 coroutine，也不额外动态分配。定义位于 [coroutine.hpp](D:/project/stdexec/include/stdexec/coroutine.hpp:175)。多组 `__awaitable_state` 特化则是为了支持不可移动的 awaitable/awaiter。

### `stdexec::task` 如何贯通两边

`stdexec::task` 是这套机制的具体落地：

- promise 继承 `with_awaitable_senders`，所以 task 内可以 `co_await sender`。
- task 提供 `.as_awaitable(parent_promise) &&`，因此 task → awaitable 走定制路径，而不是通用 `__sender_awaiter`。
- task awaiter 的 `await_suspend()` 直接返回子 task 的 coroutine handle；子 task 的 `final_suspend()` 再返回父 coroutine，实现对称转移。
- task 用作 sender 时，目前没有自己的 `.connect()`；`connect(task, receiver)` 会反过来走 awaitable fallback。

相关入口在 [__task.hpp](D:/project/stdexec/include/stdexec/__detail/__task.hpp:294)，对称转移 awaiter 在 [同文件](D:/project/stdexec/include/stdexec/__detail/__task.hpp:473)，task promise 的 sender-await 逻辑在 [同文件](D:/project/stdexec/include/stdexec/__detail/__task.hpp:568)。

task 还会在必要时给 awaited sender 包一层 `affine`，确保 coroutine 恢复到 task 的起始 scheduler。

### 两个重要结论

第一，这两条转换不是严格互逆：

- sender 的 typed error 会被 awaitable 侧擦除为异常。
- 通用 sender awaiter 会 decay 成功值，裸引用不会原样保留。
- awaitable 变 sender 时，completion signatures 保守地总会包含 `exception_ptr` 和 stopped。

第二，当前 HEAD 中 `stdexec::task` 依赖 awaitable fallback 暴露 sender 接口，因此：

```cpp
completion_signatures_of_t<task<int>, sync_wait_env>
```

实际是：

```cpp
set_value_t(int)
set_error_t(std::exception_ptr)
set_stopped_t()
```

即使自定义 `TaskEnv::error_types`，外部仍会被擦成 `exception_ptr`。而 [__task.hpp](D:/project/stdexec/include/stdexec/__detail/__task.hpp:329) 中私有的 `__completions_t` 目前没有使用者；这可能是近期移除 task 自定义 `connect/get_completion_signatures` 后留下的实现痕迹，若你关心 typed-error task，这一点值得单独审查。

对应测试覆盖了各种 `await_suspend` 返回形式、异常、停止和不可移动 awaiter：[test_cpo_connect_awaitable.cpp](D:/project/stdexec/test/stdexec/cpos/test_cpo_connect_awaitable.cpp:317)；task 的 value/error/stopped、跨线程竞争和 10000 次内联 await 则在 [test_task.cpp](D:/project/stdexec/test/stdexec/types/test_task.cpp:125)。我也用本地 Clang 对 task completion signatures、tuple 映射和引用 decay 做了静态编译验证。