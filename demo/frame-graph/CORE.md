# 帧图核心：最小实现

只写骨架和为什么。完整代码见 `frame_graph.h` / `renderer.h` / `entities.h`。

## 0. 一句话

图就是 sender 表达式。扇出 = `split`，扇入 = `when_all`，先后 = 嵌套。
框架只补三样 stdexec 没有的：**备忘格**、**动态汇合点**、**运行期扇入**。

## 1. 边

```
node_completions = completion_signatures<set_value(), set_error(exception_ptr), set_stopped()>
node_receiver    = any_receiver<node_completions, queries<inplace_stop_token(get_stop_token)>>
node_sender      = any_sender<node_receiver>

make_node(s) = node_sender{ env_gate(s) }
```

`env_gate` = 把内部看到的环境钉成固定的 `node_env`（只带一个停止令牌），
完成签名写死。**规避上游 bug**：擦除接收者的环境不可移动，而 `__fwd_env_t<_Env>` 按值转发。

## 2. 备忘格 —— 一个节点只建一次

```
node_ref:
    make: optional<() -> node_sender>
    building: bool

    get(factory):
        if not make:
            if building: throw 构建期成环
            building = true
            shared = split(factory())        # 可拷贝
            make = [shared] { make_node(shared) }   # any_sender 只可移动，所以存工厂
            building = false
        return make()
```

一格 = 一个具名共享节点。格子长在**本帧的 job** 上 ⇒ 帧结束即消失 ⇒ 不需要 `reset`。

## 3. 动态汇合点 —— 构建期挂，启动期封

```
node_roster:
    nodes: vector<node_sender>
    sealed: bool

    add(n):  if sealed: throw 注册来晚了;  nodes.push_back(n)
    seal():  sealed = true;  return move(nodes)
```

分界线由 `let_value` 免费给出：

```
end_node = split(
    begin_node()
    | let_value([] {                  # 体是【启动后】才跑的
          return when_all_range(roster.seal())
      })
    | continues_on(sched)
    | then(submit))
```

构图 → 全部挂完 → 才 connect → 才 start → 才 seal。**所以注册顺序无关。**

## 4. 运行期扇入

```
when_all_range(children).start():
    if children.empty(): set_value(); return
    pending = n + 1                    # 多出的 1 留给启动循环自己
    for c in children: connect(c, child_receiver{this})    # 先全部连
    for op in ops:     start(op)                           # 再全部启
    arrive()                           # 还掉那个 1

    child.set_value():    arrive()
    child.set_error(e):   首个胜出(e); 广播取消; arrive()
    child.set_stopped():  首个胜出(stopped); arrive()
    arrive(): if --pending == 0: 按 kind 完成给外层
```

两个坑：

- **先连后启**：反过来的话，第一个孩子同步完成时后面的还没连上。
- **`n + 1`**：否则某个同步完成的孩子会在启动循环没走完时把计数减到 0，销毁 op-state。

## 5. 取消：不走结构边

`split` 在**订阅者令牌已停止**时直接 `set_stopped`、**不启动共享体**。
fence 节点就是个 `split` ⇒ 取消若沿结构边下压，清理体一次都不跑 ⇒ slot 漏。

```
frame:
    stop_source          # 外部令牌转发进来
    structural_source    # 【从不 request_stop】

    run(external):
        callback(external, -> stop_source.request_stop())
        context.stop_token = stop_source.get_token()
        connect(when_all_range(roots), receiver{ env: structural_source.token })   # 骨架不可取消
        start; wait

cancellable(s, ctx) = write_env(s, prop{get_stop_token, ctx.stop_token})
```

干活的节点自己领取消：

```
begin_node = split(cancellable(starts_on(sched, just()) | then(open_cmd), ctx))
```

begin 不跑 ⇒ 无录制 ⇒ 无提交，而 end / fence 仍沿 stopped 路走完。

## 6. render 的三条路都要落到 fence

```
fence_node = split(
    end_node()
    | continues_on(fence_sched)
    | let_error  (e -> { wait_fence(); just_error(e) })
    | let_stopped(  -> { wait_fence(); just_stopped() })
    | then       (  -> { wait_fence() }))
```

少任何一条 ⇒ slot 的 GPU 资源没人回收。`continues_on` 只搬 value 这条路，
error / stopped 直接在出事的线程上跑。

## 7. entity 侧：同步靠自己，main 不特判

```
compute_entity.frame_node():
    return make_node(cancellable(starts_on(sched, just()) | then(step), ctx))

render_entity.frame_node():
    render = renderer_job_slot.get()          # 阶段 B：具体类型直连
    if visible: render.recorders().add(record_node(render))     # 条件性参与
    return make_node(render.fence_node() | then(cleanup))       # 帧末清理

record_node(render):
    return make_node(
        when_all(compute_node(), render.begin_node())
        | continues_on(sched)        # 【不可省】订阅已完成的 split 会原地同步派发
        | then(record))
```

非 render 的 entity 里没有任何 render 概念；main 里没有任何 `if (is_renderer)`。

## 8. 两阶段 + 帧隔离

```
frame ctor:            for e in entities: e.begin_frame(ctx)    # 阶段 A：只造 job
frame::run:            for j in jobs:     roots.push(j.frame_node())   # 阶段 B：才取根
frame dtor:            逆序销毁全部 job
```

- 阶段 A 全做完才进阶段 B ⇒ 任何 job 都能拿到任何别的 job。
- 唯一约定：**job 的构造函数里不许访问别的 job**。
- `frame` 不可拷贝不可移动；隔离是结构性的，不靠"记得清空上一帧"。
