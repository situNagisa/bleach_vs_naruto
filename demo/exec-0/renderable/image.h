// demo.renderable.image 的声明，唯一来源。三种展开形态见 execution/execution.h 开头与 README §3。
#if defined(BVN_EXEC0_MODULES) && !defined(BVN_EXEC0_MODULE_INTERFACE)

import std;
import demo.renderable.buffer_type;
import demo.renderable.image;

#else

#if !defined(BVN_EXEC0_MODULE_INTERFACE)
#include <cstdint>
#include <memory>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <bvn/assets/sprite_clip.h>

#include "buffer_type.h"
#endif

#ifndef BVN_EXEC0_RENDERABLE_IMAGE_H
#define BVN_EXEC0_RENDERABLE_IMAGE_H

struct image
{
	image(image&&) noexcept;
	image& operator=(image&&) noexcept;
	~image();

	image(::bvn::assets::image_rgba8 data, ::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format);

	[[nodiscard]] buffer_type draw(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkCommandBuffer command_buffer, ::glm::mat3x3 transform = ::glm::mat3{ 1.0f }, ::glm::vec4 tint = ::glm::vec4{ 1.0f }) const;
	[[nodiscard]] ::VkExtent2D extent() const noexcept;

	struct impl;
	::std::unique_ptr<impl> _impl;
};

#endif

#endif
