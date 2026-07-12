# 1. 概念声明

| 概念                 | 含义                                                                                                                                                                       |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **`renderer`**     | 渲染上下文（通常实现为纯数据结构），分为全局持有的`global env renderer`以及每帧持有的`frame env renderer`。如果渲染引擎是vulkan，那么渲染上下文就是vulkan的instance，device等，同理针对opengl，cuda也一样。见[renderer.md](renderer.md)。 |
| `render task`      | 负责绘制的协程，接收一个`global env renderer`参数。负责初始化自己需要的绘制环境（例如pipeline），并逐帧产生`secondary command buffer`并push进`render scheduler`中。见[render-task.md](render-task.md)                |
| **`renderable` **  | `render task`的上下文。见[renderable.md](renderable.md)                                                                                                                        |
| `render scheduler` | 控制渲染流程的调度器（将任务转发到内部持有的一个转发调度器上，本身不负责调度），控制帧开闭（槽的同步，开启primary command，present等），为`render task`提供渲染环境（例如`frame env renderer`）。见[render-scheduler.md](render-scheduler.md)  |
|                    |                                                                                                                                                                          |
