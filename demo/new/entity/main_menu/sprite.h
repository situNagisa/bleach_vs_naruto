#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include <bvn/assets/sprite_clip.h>

#include "./buffer_type.h"
#include "./sprite_texture.h"
#include "./sprite_graphic_pipeline.h"

/// A sprite clip with its own texture array, descriptors, pipeline and shaders.
struct sprite
{
	sprite_texture_type _texture;
	sprite_graphic_pipeline_type _pipeline;

	sprite(::bvn::assets::sprite_clip_data clip, VkDevice device, VkPhysicalDevice physical_device, VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, VkFormat format);

	/// Draws a zero-based animation frame in local pixel coordinates, from (0, 0) to the frame size.
	/// The caller supplies the complete transform, including conversion to viewport space.
	/// Keep the returned buffer and this sprite alive until the GPU finishes using them.
	/// @pre frame < _texture.source.frame_count.
	[[nodiscard]] auto draw(VkDevice device, VkPhysicalDevice physical_device, VkCommandBuffer command_buffer, ::std::size_t frame, ::glm::mat3x3 transform = ::glm::mat3{ 1.0f }) const -> buffer_type;
};
