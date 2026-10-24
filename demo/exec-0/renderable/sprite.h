// demo.renderable.sprite 的声明，唯一来源。三种展开形态见 execution/execution.h 开头与 README §3。
#if defined(BVN_EXEC0_MODULES) && !defined(BVN_EXEC0_MODULE_INTERFACE)

import std;
import demo.renderable.buffer_type;
import demo.renderable.sprite;

#else

#if !defined(BVN_EXEC0_MODULE_INTERFACE)
#include <cstddef>
#include <cstdint>
#include <memory>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <bvn/assets/sprite_clip.h>

#include "buffer_type.h"
#endif

#ifndef BVN_EXEC0_RENDERABLE_SPRITE_H
#define BVN_EXEC0_RENDERABLE_SPRITE_H

struct sprite
{
	sprite(sprite&&) noexcept;
	sprite& operator=(sprite&&) noexcept;
	~sprite();

	sprite(::bvn::assets::sprite_clip_data data, ::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format);

	[[nodiscard]] buffer_type draw(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkCommandBuffer command_buffer, ::std::size_t frame, ::glm::mat3x3 transform = ::glm::mat3{ 1.0f }) const;
	[[nodiscard]] ::VkExtent2D extent() const noexcept;
	[[nodiscard]] ::std::size_t frame_count() const noexcept;

	struct impl;
	::std::unique_ptr<impl> _impl;
};

#endif

#endif
