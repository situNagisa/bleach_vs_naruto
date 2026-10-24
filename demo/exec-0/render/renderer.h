// demo.render 的声明，唯一来源。三种展开形态见 execution/execution.h 开头与 README §3。
#if defined(BVN_EXEC0_MODULES) && !defined(BVN_EXEC0_MODULE_INTERFACE)

import std;
import demo.execution;
import demo.window;
import demo.render;

#else

#if !defined(BVN_EXEC0_MODULE_INTERFACE)
#include <cstdint>
#include <memory>
#include <span>

#include <vulkan/vulkan.h>

#include <nagisa/concurrency/semaphore.h>

#include "../execution/execution.h"
#include "../window/window.h"
#endif

#ifndef BVN_EXEC0_RENDER_RENDERER_H
#define BVN_EXEC0_RENDER_RENDERER_H

struct image_acquisition
{
	::std::uint32_t index;
	VkSemaphore semaphore;
	VkImage image;
	VkImageView view;
};

// frame 的定义只存在于 renderer.cpp（实现文件，全局命名空间）；接口只把它当作租约令牌的指针目标。
struct frame;

struct surface_changed {};

struct renderer
{
	using frame_pool = ::nagisa::concurrency::bounded_resource_pool<frame*, 4>;

	// window 由调用方持有，必须比 renderer 活得久；GLFW 需要先由 glfw_context 初始化。
	explicit renderer(window const& target);
	~renderer();
	renderer(renderer&&) noexcept;
	renderer& operator=(renderer&&) noexcept;
	[[nodiscard]] ::VkDevice device() const noexcept;
	[[nodiscard]] ::VkPhysicalDevice physical_device() const noexcept;
	[[nodiscard]] ::VkQueue graphics_queue() const noexcept;
	[[nodiscard]] ::std::uint32_t graphics_queue_family() const noexcept;
	[[nodiscard]] ::VkFormat format() const noexcept;
	[[nodiscard]] ::VkExtent2D extent() const noexcept;
	void resize();

	values_sender_type<frame_pool::permit> acquire_frame() const;
	values_sender_type<image_acquisition> acquire_image(frame const&) const;
	values_sender_type<> render(frame const& frame, image_acquisition const& acquisition, ::std::span<VkCommandBuffer const> commands) const;

	struct impl;
	::std::unique_ptr<impl> _impl;
};

#endif

#endif
