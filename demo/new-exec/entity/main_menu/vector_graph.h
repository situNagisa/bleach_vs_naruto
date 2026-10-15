#pragma once

#include <glm/glm.hpp>

#include "./geometry.h"
#include "./buffer_type.h"
#include "./vector_graph_graphic_pipeline.h"

struct vector_graph_type
{
	vector_graph_graphic_pipeline_type pipeline;

	vector_graph_type(VkDevice device, VkFormat format);

	/// Applies the caller's complete transform to a rectangle from (0, 0) to size.
	auto draw_rectangle(VkDevice device, VkPhysicalDevice physical_device, VkCommandBuffer command_buffer, point size, color color, ::glm::mat3x3 transform = ::glm::mat3{ 1.0f }) const noexcept -> buffer_type;
};

