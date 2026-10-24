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
	using swapchain_guard = ::nagisa::concurrency::binary_semaphore_guard;

	// 会变化的成员（交换链、图像、视图、extent）与需要外部同步的队列，定义只在 renderer.cpp。
	struct swapchain_state;

	// 交换链访问权：acquire_swapchain 的完成值，只移动。permit 存活期间别人拿不到访问权，
	// 因此 resize 不可能与本帧的 acquire_image / render 交错；permit 随本对象析构归还。
	// 需要读写 swapchain_state 的接口都要求传入它，不持有访问权就无从访问这些成员。
	struct swapchain_access
	{
		swapchain_guard::permit _permit;
		swapchain_state* _state = nullptr;

		[[nodiscard]] ::VkExtent2D extent() const noexcept;
		// Vulkan 要求队列外部同步：只有持有访问权时才拿得到图形队列。
		[[nodiscard]] ::VkQueue graphics_queue() const noexcept;
	};

	// window 由调用方持有，必须比 renderer 活得久；GLFW 需要先由 glfw_context 初始化。
	explicit renderer(window const& target);
	~renderer();
	renderer(renderer&&) noexcept;
	renderer& operator=(renderer&&) noexcept;
	// 以下都是构造后不再改变的常量，无需访问权；format 变化时 resize 会抛异常，所以也是常量。
	[[nodiscard]] ::VkDevice device() const noexcept;
	[[nodiscard]] ::VkPhysicalDevice physical_device() const noexcept;
	[[nodiscard]] ::std::uint32_t graphics_queue_family() const noexcept;
	[[nodiscard]] ::VkFormat format() const noexcept;

	// 一帧的起始点：等到上一位持有者归还 permit 后完成。
	[[nodiscard]] values_sender_type<swapchain_access> acquire_swapchain() const;
	// 重建交换链，修改 access 指向的状态；要读窗口尺寸，必须在主线程调用。
	void resize(swapchain_access& access) const;

	values_sender_type<frame_pool::permit> acquire_frame() const;
	values_sender_type<image_acquisition> acquire_image(swapchain_access const& access, frame const& frame) const;
	values_sender_type<> render(swapchain_access const& access, frame const& frame, image_acquisition const& acquisition, ::std::span<VkCommandBuffer const> commands) const;

	struct impl;
	::std::unique_ptr<impl> _impl;
};
