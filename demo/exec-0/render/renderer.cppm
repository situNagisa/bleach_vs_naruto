module;

#include <vulkan/vulkan.h>

export module demo.render;

import std;
import demo.execution;
import demo.window;

export
struct image_acquisition
{
	::std::uint32_t index;
	VkSemaphore semaphore;
	VkImage image;
	VkImageView view;
};

// frame 的定义只存在于 renderer.cpp（实现单元）；接口只把它当作租约令牌的指针目标。
export struct frame;

export struct surface_changed {};

export
struct renderer
{
	using frame_pool = ::nagisa::concurrency::bounded_resource_pool<frame*, 4>;
	using guard_type = ::nagisa::concurrency::binary_semaphore_guard;

	// 开放访问的交换链状态：只含原始 Vulkan 句柄与值，RAII 所有者留在 impl，resize 时与所有者一起更新。
	// 只有持有 guard 的 permit 时才能读；两个队列句柄虽不变也归它管，因为 Vulkan 要求同一 VkQueue 的
	// 提交 / 呈现 / 等待由外部串行化。外部只读：句柄的生命周期由 impl 里的所有者决定，写入只能经 resize。
	struct state_type
	{
		::VkQueue graphics_queue = VK_NULL_HANDLE;
		::VkQueue present_queue = VK_NULL_HANDLE;
		::VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		::VkExtent2D extent{};
		::std::vector<::VkImage> images;
		::std::vector<::VkImageView> image_views;
	};

	// window 由调用方持有，必须比 renderer 活得久；GLFW 需要先由 glfw_context 初始化。
	explicit renderer(window const& target);
	~renderer();
	renderer(renderer&&) noexcept;
	renderer& operator=(renderer&&) noexcept;
	// 以下都是构造后不再改变的常量，无需 permit；format 变化时 resize 会抛异常，所以也是常量。
	[[nodiscard]] ::VkDevice device() const noexcept;
	[[nodiscard]] ::VkPhysicalDevice physical_device() const noexcept;
	[[nodiscard]] ::std::uint32_t graphics_queue_family() const noexcept;
	[[nodiscard]] ::VkFormat format() const noexcept;

	// state 与 guard 放在 impl 里（renderer 要可移动，guard 不可移动），经这两个成员函数取用。
	[[nodiscard]] state_type const& state() const noexcept;
	[[nodiscard]] guard_type& guard() const noexcept;

	// 一帧的起始点：等到上一位持有者归还 permit 后完成，完成值只有 permit。
	// 没有变化值：state 唯一的修改者是 resize，而 resize 要求调用方持有 permit，持有者自己就知道是否重建过。
	// renderer 不析构 permit：调用方在 GPU、present 都完成后自行销毁。
	[[nodiscard]] values_sender_type<guard_type::permit> acquire_swapchain() const;
	// 重建交换链并更新 state；调用方必须持有 permit。要读窗口尺寸，必须在主线程调用。
	void resize(guard_type::permit const& permit) const;

	values_sender_type<frame_pool::permit> acquire_frame() const;
	// 以下读 state 并使用队列，调用方必须持有 permit，且 permit 要活过返回的 sender。
	values_sender_type<image_acquisition> acquire_image(guard_type::permit const& permit, frame const& frame) const;
	values_sender_type<> render(guard_type::permit const& permit, frame const& frame, image_acquisition const& acquisition, ::std::span<VkCommandBuffer const> commands) const;

	struct impl;
	::std::unique_ptr<impl> _impl;
};
