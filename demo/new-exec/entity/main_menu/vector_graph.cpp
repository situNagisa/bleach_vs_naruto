#include <array>
#include <cstdint>
#include <span>

#include "./vector_graph.h"

vector_graph_type::vector_graph_type(
	VkDevice device
	, VkFormat format
)
	: pipeline(device, format)
{}

auto vector_graph_type::draw_rectangle(
	VkDevice device
	, VkPhysicalDevice physical_device
	, VkCommandBuffer command_buffer
	, point size
	, color color
	, ::glm::mat3x3 transform
) const noexcept -> buffer_type
{
	auto transform_point = [&](point p) -> point
		{
			auto const result = transform * ::glm::vec3{ p.x, p.y, 1.0f };
			return { result.x, result.y };
		};

	auto const p0 = transform_point({ 0.0f, 0.0f });
	auto const p1 = transform_point({ size.x, 0.0f });
	auto const p2 = transform_point({ size.x, size.y });
	auto const p3 = transform_point({ 0.0f, size.y });

	geometry g;
	g.rectangle(p0, p1, p2, p3, color);

	::vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
	buffer_type result(device, physical_device, ::std::as_bytes(::std::span{ g.vertices }), ::vkfu::flags::buffer_usage{ .vertex_buffer = 1 });
	::vkfu::cmd_bind_vertex_buffers(command_buffer, 0, ::std::array{ result.handle.handle }, ::std::array{ ::VkDeviceSize{} });
	::vkCmdDraw(command_buffer, static_cast<::std::uint32_t>(g.vertices.size()), 1, 0, 0);
	return result;
}

