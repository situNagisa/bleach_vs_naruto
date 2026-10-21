module;

#include <vulkan/vulkan.h>
#include <bvn/platform/window.h>

#include <nagisa/concurrency/lease.h>

export module demo.render;

import std;
import demo.execution;

export
struct image_acquisition
{
	::std::uint32_t index;
	VkSemaphore semaphore;
	VkImage image;
	VkImageView view;
};

export struct frame;

export
struct renderer
{
	using frame_pool = ::nagisa::concurrency::bounded_lease_pool<frame*, 4>;

	explicit renderer();
	~renderer();
	renderer(renderer&&) noexcept;
	renderer& operator=(renderer&&) noexcept;

	values_sender_type<frame_pool::lease> acquire_frame() const;
	values_sender_type<image_acquisition> acquire_image(frame const&) const;
	values_sender_type<> render(frame const& frame, image_acquisition const& acquisition, ::std::span<VkCommandBuffer const> commands) const;

	struct impl;
	::std::unique_ptr<impl> _impl;
};
