# 完整设计：以 `Batch` 为核心，覆盖多队列所有权转移和多线程 secondary 合并

## 设计基线（不重复推导过程，只列约束）

1. `Batch` 的四个操作（`record`/`cut`/`wait_for`/`signal_on_finish`）内部**不允许有任何基于资源状态的判断**。它们是纯粹的机械执行——录哪条指令、切不切批次，全部由上游 sender 节点（`transition`/`cut_batch`）已经决定好，`Batch` 不重新判断一次。
2. `current_batch()` 不是全局查找。**Batch 的引用/句柄是算子状态的字段，在 `connect` 时通过上游传下来**，跟 encoder 的传递方式一致，没有运行时查表。
3. 命令缓冲从预分配的池子里借，`cut()` 是"归还当前的、借下一个"，不是当场分配。

---

## 第一部分：`Batch` 到底是什么——先分离"录制目标"和"提交计划"

这是我之前设计里最大的空白。重新想清楚后，`Batch` 不应该是一个单一对象，而要拆成两层，否则"多线程并行录制多个 secondary、最后按声明顺序合并"和"多队列"这两件事没有地方放：

```
一个 "SubmitPlan" 存在于整条链的作用域内(对应一次 boundary_operation 的生命周期)：
    它不是运行时被查询出来的,是从最外层 boundary_operation 一路往下游按引用传递的

一个 SubmitPlan 拥有若干 QueueTrack（每个用到的队列一条）：
    QueueTrack 拥有若干 SubmitBatch（这条队列上顺序提交的一批一批)：
        SubmitBatch 拥有一个 primary 命令缓冲 + 若干挂起的 secondary 槽位（用于并发录制合并）
```

```cpp
// ---------------------------------------------------------------------
// 最底层：一批要提交的东西。只做机械记录，不做判断。
// ---------------------------------------------------------------------
struct SubmitBatch : immovable
{
    VkQueue _queue;
    ::vkkl::command_pool _pool;
    ::vkkl::command_buffer _primary;           // 这一批的 primary 命令缓冲，池里借来的
    ::std::vector<VkSemaphoreSubmitInfo> _waits{};
    ::std::vector<VkSemaphoreSubmitInfo> _signals{};

    // 并发录制支持：secondary 槲位按"声明顺序"占位，不是按"完成顺序"填充
    struct pending_secondary
    {
        ::std::optional<::vkkl::command_buffer> _buffer{};  // 填好之前是空的
    };
    ::std::deque<pending_secondary> _secondary_slots{};
    ::std::mutex _secondary_mutex{};             // 只保护 _secondary_slots 的插入/填充，不做任何决策

    // ---- 单线程主命令流用这个：直接录进 primary ----
    auto record(RecordedOp const& op) -> void
    {
        op.apply(_primary.handle);               // 纯粹的 vkCmd* 调用转发，没有 if
    }

    // ---- 并发录制用这个：占一个位置,拿到位置的句柄用来自己填 ----
    // 调用顺序 = 声明顺序（when_all 的分支在 connect 时就按源码里出现的顺序占位）
    auto reserve_secondary() -> ::std::size_t
    {
        auto lock = ::std::scoped_lock{_secondary_mutex};
        _secondary_slots.emplace_back();
        return _secondary_slots.size() - 1;
    }

    // 哪个线程录完就填哪个槲位,不要求按顺序完成,只要求按顺序占位
    auto fill_secondary(::std::size_t slot, ::vkkl::command_buffer buffer) -> void
    {
        auto lock = ::std::scoped_lock{_secondary_mutex};
        _secondary_slots[slot]._buffer = ::std::move(buffer);
    }

    // 合并阶段（在 cut/finalize 时机械调用，此时所有并发录制必须已经全部完成——
    // 这个"必须已完成"的保证由 when_all 语义提供，不是 Batch 自己判断的）
    auto flatten_secondary_into_primary() -> void
    {
        auto handles = _secondary_slots
            | ::std::views::transform([](auto& slot) { return slot._buffer->handle; })
            | ::std::ranges::to<::std::vector>();
        // 按占位顺序（=声明顺序）execute，不是按填充顺序——这是修正过 render.h 里那个
        // "push_back 按完成顺序、导致绘制顺序不固定"的 bug 的地方
        ::vkfu::cmd_execute_commands(_primary.handle, handles);
    }

    auto finalize_with_signal(::VkSemaphore timeline, ::std::uint64_t value) -> void
    {
        flatten_secondary_into_primary();
        check(::vkEndCommandBuffer(_primary.handle), "batch: end command buffer");
        _signals.push_back(::vkfu::evaluate(::vkfu::param::semaphore_submit{
            .semaphore = timeline, .value = value, .stage_mask = {.all_commands = 1}}));
        auto const cmd = ::vkfu::evaluate(::vkfu::param::command_buffer_submit{.command_buffer = _primary.handle});
        auto const submit = ::vkfu::evaluate(::vkfu::param::submit2{
            .wait_semaphore_infos = _waits, .command_buffer_infos = ::std::span{&cmd, 1u},
            .signal_semaphore_infos = _signals});
        check(::vkQueueSubmit2(_queue, 1, &submit, VK_NULL_HANDLE), "batch: submit");
    }

    auto wait_for(::VkSemaphore signal, ::VkPipelineStageFlags2 stage) -> void
    {
        _waits.push_back(::vkfu::evaluate(::vkfu::param::semaphore_submit{.semaphore = signal, .stage_mask = stage}));
    }
};
```

## 第二部分：`QueueTrack`——同一个队列上按顺序排列的多个 `SubmitBatch`

多队列所有权转移意味着：一个资源可能这一批在 graphics 队列上被写、下一批要在 compute 队列上被读，中间必须走 `srcQueueFamilyIndex`/`dstQueueFamilyIndex` 的 release/acquire 一对 barrier，且**必须通过 semaphore 衔接**（不同队列之间没有命令缓冲内 barrier 这回事）。所以 `QueueTrack` 是"每个队列自己的一条 batch 序列"：

```cpp
struct QueueTrack : immovable
{
    ::VkQueue _queue;
    ::std::uint32_t _family;
    ::std::unique_ptr<SubmitBatch> _current;    // 当前正在录制的批次
    context* _ctx;

    auto current() -> SubmitBatch& { return *_current; }

    // 无条件切一批：归还当前的（触发它 submit），借一个新的。
    // "要不要切"从来不是这里判断的，是调用方（cut_batch sender）已经决定要切了才调这个
    auto cut() -> void
    {
        auto const value = _ctx->next_timeline_value();       // 队列锁内单调分配（沿用第一层的规则）
        _current->finalize_with_signal(_ctx->timeline(), value);
        _ctx->reactor().track(value, /*谁在等它,由消费方通过 wait_for 记录*/);
        _current = _ctx->borrow_batch(_queue);                 // 从池子借下一个，不现场分配
    }
};
```

## 第三部分：`SubmitPlan`——多队列的汇总，只是一个查表，不做调度决策

```cpp
struct SubmitPlan : immovable
{
    context* _ctx;
    ::std::unordered_map<::VkQueue, ::std::unique_ptr<QueueTrack>> _tracks{};

    // 按队列句柄找到/创建对应的 track——这是"分派到哪条队列"的唯一判断点，
    // 且判断依据是"这个节点的 queue 参数"（人在写 sender 时指定的），不是资源状态推出来的
    auto track_for(::VkQueue queue) -> QueueTrack&
    {
        auto it = _tracks.find(queue);
        if (it == _tracks.end())
            it = _tracks.emplace(queue, _ctx->make_track(queue)).first;
        return *it->second;
    }
};
```

**这里唯一的"运行时查找"是一个按 `VkQueue` 键值的 map 查询**——这是不可避免的固有成本：一条链在运行时才知道自己被路由到了具体哪个 `VkQueue`（多队列场景下，人写 sender 的时候是知道的，但这个信息要从 sender 类型参数传到 `SubmitPlan` 手上，最省事且不引入额外状态机的方式就是按句柄查表）。如果只有一个队列（绝大多数场景），这张表只有一条记录，查找开销可忽略。

## 第四部分：sender 层怎么拿到 `Batch`/`QueueTrack`——沿用"env 传递"，不查全局

```cpp
// 环境查询：跟 get_encoder 一样的模式,不是全局状态
struct get_queue_track_t : ::stdexec::forwarding_query_t
{
    template <class Env> auto operator()(Env const& env) const noexcept -> QueueTrack&
    { return env.query(*this); }
};
inline constexpr get_queue_track_t get_queue_track{};

// transition/act 内部用法：拿引用,直接调用,没有查找
operation::on_predecessor_value(item: R):
    auto& track = get_queue_track(get_env(receiver));
    auto barrier = Descriptor::build(item.handle(), item.state(), To{});
    track.current().record(RecordedOp::pipeline_barrier(barrier));
    set_value(receiver, item.with_state(To{}));
```

## 第五部分：多队列所有权转移——独立的一对节点，不是 `transition` 的变体

```cpp
// release：在源队列上声明"这个资源要移交给另一个队列族了"
template <class To, tracked_resource R>
auto release_ownership(sender_of<R> pred, ::std::uint32_t dst_family)
    -> sender_of<pending<typename R::handle_type, To, ::VkSemaphore>>
{
    operation::on_predecessor_value(item: R):
        auto& track = get_queue_track(get_env(receiver));
        auto barrier = Descriptor::build_with_family_transfer(item.handle(), item.state(), To{},
                                                                 /*src=*/track.family(), dst_family);
        track.current().record(RecordedOp::pipeline_barrier(barrier));
        auto signal = track.current().finalize_with_signal_for_transfer();  // 强制切批次,拿到信号
        set_value(receiver, pending{item.handle(), signal});
}

// acquire_ownership：在目标队列上消费上一步的信号,完成所有权转移的另一半
template <class To, pending_resource R>
auto acquire_ownership(sender_of<R> pred, ::std::uint32_t src_family, QueueTrack& dst_track)
    -> sender_of<typename R::template rebind<To>>
{
    operation::on_predecessor_value(item: R):
        dst_track.current().wait_for(item.signal(), To::stage());
        auto barrier = Descriptor::build_with_family_transfer(item.handle(), /*from placeholder*/, To{},
                                                                 src_family, dst_track.family());
        dst_track.current().record(RecordedOp::pipeline_barrier(barrier));
        set_value(receiver, item.template rebind<To>{item.handle(), To{}});
}
```

链条示例：

```cpp
schedule(gpu, graphics_track)
  | act([](VkBuffer b) { vkCmdCopyBuffer(cmd, ..., b); })              // graphics 队列上写
  | release_ownership<compute_read>(dst_family = compute_family)        // graphics 侧 release,强制切批提交
  | continues_on(compute_scheduler)                                    // 切到 compute 队列的录制上下文
  | acquire_ownership<compute_read>(src_family = graphics_family, compute_track)  // compute 侧 acquire
  | act([](VkBuffer b) { vkCmdDispatch(...); })                        // compute 队列上读
```

**没有引入新的判断逻辑**——`release_ownership`/`acquire_ownership` 就是"两条队列各自记一条 barrier + 中间用 semaphore 衔接"这件事的直接翻译，人必须显式写这一对节点（对应手写 Vulkan 时你自己知道要写 release/acquire 一对 barrier），不是系统自动检测"这个资源要跨队列了所以自动转移"。

## 第六部分：多线程并发录制 secondary、合并——回答"when_all 的分支怎么进 Batch"

```cpp
// command_node_secondary：不直接录进 track.current() 的 primary，
// 而是占一个槽位，自己拿一个独立的 secondary 命令缓冲去录，录完再填回槽位
template <tracked_resource R>
auto act_concurrent(sender_of<R> pred, std::invocable<VkCommandBuffer, typename R::handle_type> auto body)
    -> sender_of<R>
{
    operation::on_predecessor_value(item: R):
        auto& track = get_queue_track(get_env(receiver));
        auto slot = track.current().reserve_secondary();          // 占位,顺序 = 这行代码在 when_all 分支里的声明顺序
        auto secondary = borrow_secondary_command_buffer(track);   // 独立的池子借出来，不跟其他线程抢
        begin_secondary(secondary, track.current().inheritance_info());
        body(secondary.handle, item.handle());
        end_secondary(secondary);
        track.current().fill_secondary(slot, ::std::move(secondary));   // 填回去,顺序无关,靠 slot index 归位
        set_value(receiver, item);
}

// 用法：when_all 的每个分支各自 act_concurrent,合流后统一 cut，flatten 会按占位顺序 execute
when_all(
    act_concurrent(resource_a, draw_a),
    act_concurrent(resource_b, draw_b),
    act_concurrent(resource_c, draw_c)
)
| continues_on(track.scheduler())
| cut_batch()   // 触发 finalize_with_signal → flatten_secondary_into_primary()，按声明顺序合并
```

`reserve_secondary` 的调用时机是**each 分支的 `on_predecessor_value`**，也就是 `when_all` 内部按分支声明顺序依次 connect/start 的时刻——这保证了槲位顺序=声明顺序，跟哪个分支先跑完无关，直接对应到之前推演里"合流要按声明顺序，不能按完成顺序"这条要求。

## 完整覆盖清单，对照你提的两个新要求

| 要求 | 落地位置 |
|---|---|
| 多队列所有权转移 | `QueueTrack` 分队列持有独立的 `SubmitBatch` 序列；`release_ownership`/`acquire_ownership` 一对节点，各自在自己队列的 track 上记barrier，中间用 semaphore（走 `finalize_with_signal_for_transfer`）衔接 |
| 多线程并发录制 secondary 并按顺序合并 | `SubmitBatch::reserve_secondary`/`fill_secondary`/`flatten_secondary_into_primary`，占位顺序=声明顺序，填充可以乱序完成，合并时机械按顺序 `cmd_execute_commands` |

## 这次设计里，哪些地方仍然是"不可避免的固有开销"，哪些是我要主动指出的风险点

- **固有**：`SubmitPlan::track_for` 按 `VkQueue` 查 map——多队列场景下这是必须的路由信息，单队列场景下退化成一条记录，几乎免费。`_secondary_mutex` 的锁——多线程并发填槲位，这是并发本身要求的，不是设计强加的。
- **仍需警惕**：`SubmitBatch::flatten_secondary_into_primary` 依赖"所有槲位在调用时都已填满"这个前提，如果 `when_all` 的某个分支异常/取消，槲位会永久是空的，`flatten` 会在解引用 `_buffer->handle` 时炸——这个错误处理路径我还没有设计,是这版遗留的缺口，需要在 `when_all` 的取消传播和 `cut_batch` 之间补一层"确认全部完成才合并,否则整体走 stopped/error 路径"的机制，目前只有"正常路径"是完整的。


# `Batch` 设计：精简版

## 三层结构

```
Batch    —— 一个命令缓冲 + 若干并发录制的槽位
Track    —— 一个队列上，一批接一批的 Batch 序列
Plan     —— 按队列分出多个 Track（只有跨队列才需要不止一个）
```

## Batch：只做机械记录，不做判断

```
struct Batch:
    cmd: CommandBuffer
    waits:   [Semaphore]
    signals: [Semaphore]
    slots:   [optional<CommandBuffer>]     // 并发录制用的占位槽

    record(op):
        op.apply(cmd)                       // 直接转发一条 vkCmd*，没有 if

    reserve_slot() -> index:
        slots.push(empty)
        return slots.length - 1             // 顺序 = 调用顺序，不是完成顺序

    fill_slot(index, secondary_cmd):
        slots[index] = secondary_cmd        // 谁先录完谁先填，顺序无关

    wait_for(signal, stage):
        waits.push(signal, stage)

    submit(timeline, value) -> Semaphore:
        for s in slots: execute_commands(cmd, s)   // 按占位顺序合并，不按完成顺序
        end(cmd)
        signals.push(timeline, value)
        queue_submit(cmd, waits, signals)
        return timeline@value
```

## Track：一个队列自己的一条 Batch 链

```
struct Track:
    queue: Queue
    family: uint32
    current: Batch                          // 从池子借来的，不现场分配

    cut():                                   // 无条件切一批，谁调用谁决定要不要切
        signal = current.submit(timeline, next_value())
        current = pool.borrow(queue)
        return signal
```

## Plan：只在多队列时才需要，按队列查表

```
struct Plan:
    tracks: map<Queue, Track>

    track_for(queue) -> Track:
        if queue not in tracks: tracks[queue] = new Track(queue)
        return tracks[queue]
```

单队列场景，`tracks` 只有一条记录，这一步几乎无成本；这是多队列本身要求的路由信息，不是额外开销。

---

# 用法一：多队列所有权转移

```
release<To>(pred, dst_family):
    on_value(item):
        track = env.track                                   // 当前所在的队列 track，引用传递，不查找
        track.record(barrier(item.state, To, transfer_to=dst_family))
        signal = track.cut()                                 // 强制切批，拿到跨队列要用的信号
        emit(pending(item.handle, signal))

acquire_ownership<To>(pred, src_family, dst_track):
    on_value(item):
        dst_track.wait_for(item.signal, To.stage)
        dst_track.record(barrier(from=external, To, transfer_from=src_family))
        emit(tracked(item.handle, To))
```

链：

```
graphics_track
  | act(copy_into_buffer)
  | release<compute_read>(dst_family = compute_family)
  | continues_on(compute_scheduler)
  | acquire_ownership<compute_read>(src_family = graphics_family, compute_track)
  | act(dispatch_read)
```

一对节点，各自在自己的 `Track` 上记一条 barrier，中间用 `cut()` 产出的信号衔接——跟手写代码"两条队列各自插一条转移 barrier，中间一个 semaphore"逐行对应。

---

# 用法二：多线程并发录制多个 secondary，按声明顺序合并

```
act_concurrent(pred, body):
    on_value(item):
        track = env.track
        slot = track.current.reserve_slot()        // 占位，顺序 = 这一行在链里出现的顺序
        secondary = borrow_secondary_buffer()
        body(secondary, item.handle)                // 这里才是真正并发发生的地方
        track.current.fill_slot(slot, secondary)    // 填回去，跟其他分支谁先谁后无关
        emit(item)
```

链：

```
when_all(
    act_concurrent(resource_a, draw_a),
    act_concurrent(resource_b, draw_b),
    act_concurrent(resource_c, draw_c)
)
| cut_batch()          // 触发 submit → 按 a, b, c 的占位顺序 execute_commands，不按完成顺序
```

## 没解决的问题（如实说明）

某个分支异常/取消时，它对应的槽位永远填不满，`submit()` 遍历到空槽位会出错——目前没有设计"取消/错误时如何让整批安全放弃"，这是需要补的部分,不是已经解决、只是没写出来。