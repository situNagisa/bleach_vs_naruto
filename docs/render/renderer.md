# bvn 渲染·renderer（设计 + 规范）

> renderer 的设计、形态、职责，以及给 render task 的接口边界。
> 本文只讲规范级；一帧命令编排与 generation 生命周期见 [render-scheduler.md](render-scheduler.md#bvn-渲染render-scheduler设计-规范)。

---

## 1. renderer 的形态与职责划分

renderer 不是一个具体类型，而是一组 **concept**：它约束「一个东西要能当 renderer，得能读出哪些渲染环境」，而不规定这些环境背后是谁。

renderer 携带 render task 所需的渲染上下文。render task 是长期协程，参数只在创建时传入一次；因此 renderer 按生命周期拆成两部分：global env 在协程启动时传入，每帧变化的观察值随 `async_record(pool)` 的结果取得。frame slot 的选择与轮转留在 render workflow 内部。

现阶段实现直接使用 Vulkan，并从实际需求萃取 renderer concept；render task 依赖 concept，而不是资源 owner 的具体类型。

### 1.0 两个 env renderer

renderer 边界按生命周期拆成两个独立 concept：

| concept               | 生命周期                  | render task 何时用                   | 典型访问器                                                                                                                             |
| --------------------- | --------------------- | --------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| `global_env_renderer` | 整段 render task 生命周期稳定 | 初始化期建 pipeline / buffer / texture | `device()` / `physical_device()` / `graphics_queue()` / `graphics_queue_family()` / `swapchain_image_format()` / `depth_format()` |
| `frame_env_renderer`  | 某一帧、某个 task 被放行录制时有效  | 读取本帧 image / depth 等观察值           | `in_flight()` / `image_available()` / `active_image_index()` / `depth_image()`                                                     |

### 1.1 `global_env_renderer` concept

参考实现：
```cpp
template <class R>
concept global_env_renderer = requires(R const& r)
{
	{ r.instance() } -> ::std::convertible_to<VkInstance const&>;
	{ r.physical_device() } -> ::std::convertible_to<VkPhysicalDevice const&>;
	{ r.device() } -> ::std::convertible_to<VkDevice const&>;
	{ r.graphics_queue() } -> ::std::convertible_to<VkQueue const&>;
	{ r.graphics_queue_family() } -> ::std::convertible_to<::std::uint32_t>;
	{ r.swapchain() } -> ::std::convertible_to<VkSwapchainKHR const&>;
	...
};
```
### 1.2 `frame_env_renderer` concept
参考实现：
```cpp
template <class R>
concept frame_env_renderer = requires(R const& r)
{
	{ r.in_flight() } -> ::std::convertible_to<VkFence const&>;
	{ r.image_available() } -> ::std::convertible_to<VkSemaphore const&>;
	{ r.active_image_index() } -> ::std::convertible_to<::std::uint32_t>;
	{ r.depth_image() } -> ::std::convertible_to<VkImage const&>;
	{ r.depth_image_view() } -> ::std::convertible_to<VkImageView const&>;
	...
};
```

### 1.3  满足concept的类（vulkan参考实现）

比较推荐的实现思路是用一个纯数据结构作资源owner，再由他产生一个轻量的类型`T`，由这个类型`T`去实现`global/frame_env_renderer`概念
#### 1.3.1 `global_env_renderer`

```cpp
// 纯数据结构，掌管成员的生命周期（拥有所有权）
struct vulkan_context
{
	// 存储对象实体
	instance;
	device;
	...
};

// 轻量的转发对象，满足concept
struct global_vulkan_env_renderer
{
	vulkan_context const* _context = nullptr;

	auto instance() const noexcept -> VkInstance;
	auto physical_device() const noexcept -> VkPhysicalDevice;
	auto device() const noexcept -> VkDevice;
	auto graphics_queue() const noexcept -> VkQueue;
	auto graphics_queue_family() const noexcept -> ::std::uint32_t;
	auto swapchain() const noexcept -> VkSwapchainKHR;
};
static_assert(global_env_renderer<global_vulkan_env_renderer>);
```

`vulkan_context::global_env()` 返回上述轻量 observer；frame observer 由 workflow 按本轮 slot / image 直接构造。
#### 1.3.2 `frame_env_renderer`

frame 侧按 owner / observer 分工：

| 类型 | 角色 |
|---|---|
| `vulkan_context` | 拥有 device、swapchain、primary frame slot 等 Vulkan 数据 |
| `render_workflow` | 拥有一帧编排状态、frame generation 与完成通知 |
| render task 的 `secondary_command_pool` | 拥有该 task 的 secondary Vulkan pool 与全部 secondary buffer |
| `frame_vulkan_env_renderer` | 观察 `vulkan_context` 的当前 slot / image，不拥有资源 |
| `render_recording_frame` | 组合 frame renderer observer、generation shared state 与 task pool observer |

`async_record(pool)` 返回 `render_recording_frame`。它满足 `frame_env_renderer` concept，同时提供本轮 command 分配与 retirement 入口：

```cpp
struct frame_vulkan_env_renderer
{
	vulkan_context const* _context = nullptr;
	::std::uint32_t _slot_index = 0;
	::std::uint32_t _active_image_index = 0;

	auto in_flight() const noexcept -> VkFence;
	auto active_image_index() const noexcept -> ::std::uint32_t;
	auto depth_image() const noexcept -> VkImage;
};

struct render_recording_frame : frame_vulkan_env_renderer
{
	::std::shared_ptr<render_frame_state> _state;
	secondary_command_pool* _commands = nullptr;

	auto allocate() const -> secondary_command_recording;
	auto retire(secondary_command_recording command) const noexcept
		-> secondary_command_retirement;
};
```

`render_recording_frame` 是短生命周期观察值；资源所有权仍分别留在 renderer、workflow 与 render task。精确回收契约见 [render-scheduler.md §3](render-scheduler.md#3-帧录制与回收)。

### 1.4 转发

render task 可能跨 DLL。模块内部优先使用静态转发保留具体类型；跨 ABI 边界时使用动态转发擦除类型。两者暴露同一组 renderer accessor，返回容器值时直接返回 `vector`。

#### 1.4.1 静态转发

```cpp
template <class R>
struct global_forward_env_renderer
{
	R _inner;

	constexpr auto instance() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).instance();
	}
	...
};
```

`frame_forward_env_renderer<R>` 使用相同结构转发 frame accessor。具体 Vulkan observer 使用 §1.3.1 的 `global_vulkan_env_renderer`；静态转发模板用于其余需要转发的 renderer wrapper。

#### 1.4.2 动态转发

```cpp
namespace renderer_dynamic_forward
{
	struct basic_global_env_renderer
	{
		virtual ~basic_global_env_renderer() noexcept = default;
		virtual auto instance() const noexcept -> VkInstance = 0;
		virtual auto swapchain_images() const -> ::std::vector<VkImage> = 0;
		...
	};

	template <class R>
	struct global_env_renderer_eraser : basic_global_env_renderer
	{
		R _inner;

		auto instance() const noexcept -> VkInstance override
		{
			return renderer_forward::dereference(_inner).instance();
		}
		...
	};
}

struct global_dynamic_forward_env_renderer
{
	::std::unique_ptr<renderer_dynamic_forward::basic_global_env_renderer> _inner;

	auto instance() const noexcept -> VkInstance
	{
		return _inner->instance();
	}
	...
};

template <global_env_renderer R>
auto dynamic_forward_global_env_renderer(R&& renderer)
	-> global_dynamic_forward_env_renderer;
```

frame dynamic forwarding 使用同一层次；两组 dynamic wrapper 都按值持有 `unique_ptr`，是 type-erased renderer 的 owner。

---

## 2. 帧生命周期（五阶段·规范级）

### 2.1 持久环境

程序启动时建立`global_env_renderer`所需要的资源（ 例如初始化`vulkan_context`）。
### 2.2 每帧环境

`render_workflow::submit()` 在所有 task 前执行 wait slot fence、完成该 slot 的上一 generation、acquire / reset / begin primary、layout barrier 与 `vkCmdBeginRendering`。

当前 primary 以 secondary contents 模式开启 dynamic rendering：

```cpp
VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT
```

### 2.3 录制期

每个等待中的 render task 由 `async_record(task_pool)` 恢复，得到一个 `render_recording_frame`。task 通过 `frame.allocate()` 从自己的 pool 取得 secondary command buffer 并预留未发布条目，再录制：

```cpp
vkCmdBindPipeline(secondary_cmd, ...);
vkCmdSetViewport(secondary_cmd, ...);
vkCmdSetScissor(secondary_cmd, ...);
vkCmdBindVertexBuffers(secondary_cmd, ...);
vkCmdBindDescriptorSets(secondary_cmd, ...);
vkCmdPushConstants(secondary_cmd, ...);
vkCmdDraw(secondary_cmd, ...);
```

第三方后端（如 ImGui Vulkan backend）也必须只把 draw 录进该 secondary command buffer。

录制成功后，`frame.retire()` 的 operation start 原子发布 command 并绑定本 generation；task 自身不维护或轮转 frame slot。资源归属与完整代码形态见 [render-task.md §1](render-task.md#1-render-task-的形态)。

### 2.4 结束一帧

本帧全部 task 录完后，`submit()` 在 primary 中执行 generation 收集的 secondaries，然后 end rendering、转 present layout、submit、present、轮转 frame slot。成功提交的 generation 绑定到实际 slot，等待其 fence 后才完成。

### 2.5 结束全部

GPU 停止使用 task 自建 Vulkan 资源后，workflow 先完成在途 generation，task 再 join retirement scope 并析构资源，device 最后析构。完整收尾编排见 [render-scheduler.md §5](render-scheduler.md#5-收尾次序)。
