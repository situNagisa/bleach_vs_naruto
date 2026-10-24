// demo.renderable.vector_graph 的声明，唯一来源。三种展开形态见 execution/execution.h 开头与 README §3。
#if defined(BVN_EXEC0_MODULES) && !defined(BVN_EXEC0_MODULE_INTERFACE)

import std;
import demo.renderable.buffer_type;
import demo.renderable.vector_graph;

#else

#if !defined(BVN_EXEC0_MODULE_INTERFACE)
#include <array>
#include <memory>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include "buffer_type.h"
#endif

#ifndef BVN_EXEC0_RENDERABLE_VECTOR_GRAPH_H
#define BVN_EXEC0_RENDERABLE_VECTOR_GRAPH_H

struct vector_graph
{
	struct point
	{
		float x{}, y{};
	};
	using color = ::std::array<float, 4>;

	vector_graph(::VkDevice device, ::VkFormat format);
	~vector_graph();
	vector_graph(vector_graph&&) noexcept;
	vector_graph& operator=(vector_graph&&) noexcept;

	[[nodiscard]] buffer_type draw_rectangle(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkCommandBuffer command_buffer, point size, color fill, ::glm::mat3x3 transform = ::glm::mat3{ 1.0f }) const;

	struct impl;
	::std::unique_ptr<impl> _impl;
};

#endif

#endif
