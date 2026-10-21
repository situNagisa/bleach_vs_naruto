module;

#include <vulkan/vulkan.h>

#include <nagisa/concurrency/lease.h>

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

export
struct renderer
{
	using frame_pool = ::nagisa::concurrency::bounded_lease_pool<frame*, 4>;

	// window 由调用方持有，必须比 renderer 活得久；GLFW 需要先由 glfw_context 初始化。
	explicit renderer(window const& target);
	~renderer();
	renderer(renderer&&) noexcept;
	renderer& operator=(renderer&&) noexcept;

	values_sender_type<frame_pool::lease> acquire_frame() const;
	values_sender_type<image_acquisition> acquire_image(frame const&) const;
	values_sender_type<> render(frame const& frame, image_acquisition const& acquisition, ::std::span<VkCommandBuffer const> commands) const;

	struct impl;
	::std::unique_ptr<impl> _impl;
};
