#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "./buffer_type.h"
#include "./texture.h"
#include "./graphic_resource.h"

struct background
{
	texture_graphic_pipeline_type pipeline;
	texture_type texture;

	background(::bvn::assets::image_rgba8 img, VkDevice device, VkPhysicalDevice physical_device, VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, VkFormat format);

	/// Applies the caller's complete transform to image coordinates.
	auto draw(VkDevice device, VkPhysicalDevice physical_device, VkCommandBuffer command_buffer, ::glm::mat3x3 transform = ::glm::mat3{ 1.0f }) const noexcept -> buffer_type;
};

