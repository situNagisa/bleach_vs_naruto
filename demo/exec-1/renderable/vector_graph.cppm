module;

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

export module demo.renderable.vector_graph;

import std;
export import demo.renderable.buffer_type;

export struct vector_graph
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
