#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <utility>

#include "./sprite.h"
#include "./sprite_vertex.h"

sprite::sprite(
	::bvn::assets::sprite_clip_data clip
	, VkDevice device
	, VkPhysicalDevice physical_device
	, VkQueue graphics_queue
	, ::std::uint32_t graphics_queue_family
	, VkFormat format
)
	: _texture(::std::move(clip), device, physical_device, graphics_queue, graphics_queue_family)
	, _pipeline(device, format, _texture.sampler.handle, _texture.view.handle)
{}

auto sprite::draw(
	VkDevice device
	, VkPhysicalDevice physical_device
	, VkCommandBuffer command_buffer
	, ::std::size_t frame
	, ::glm::mat3x3 transform
) const -> buffer_type
{
	assert(frame < _texture.source.frame_count);

	auto const extent = ::glm::vec2{
		static_cast<float>(_texture.source.frame_width),
		static_cast<float>(_texture.source.frame_height)
	};
	auto transform_point = [&](::glm::vec2 position) -> ::glm::vec2
		{
			auto const result = transform * ::glm::vec3{ position, 1.0f };
			return { result.x, result.y };
		};
	auto const p0 = transform_point({ 0.0f, 0.0f });
	auto const p1 = transform_point({ extent.x, 0.0f });
	auto const p2 = transform_point(extent);
	auto const p3 = transform_point({ 0.0f, extent.y });
	auto const vertices = ::std::array{
		sprite_vertex_type{ p0, { 0.0f, 0.0f } },
		sprite_vertex_type{ p1, { 1.0f, 0.0f } },
		sprite_vertex_type{ p2, { 1.0f, 1.0f } },
		sprite_vertex_type{ p0, { 0.0f, 0.0f } },
		sprite_vertex_type{ p2, { 1.0f, 1.0f } },
		sprite_vertex_type{ p3, { 0.0f, 1.0f } }
	};
	auto result = buffer_type{ device, physical_device, ::std::as_bytes(::std::span{ vertices }), ::vkfu::flags::buffer_usage{ .vertex_buffer = 1 } };
	auto const layer = static_cast<::std::uint32_t>(frame);
	::vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline.pipeline.handle);
	::vkfu::cmd_bind_descriptor_sets(command_buffer, ::vkfu::enums::pipeline_bind_point::graphics, _pipeline.layout.handle, 0, ::std::array{ _pipeline.descriptor.handle }, {});
	::vkCmdPushConstants(command_buffer, _pipeline.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(layer), &layer);
	::vkfu::cmd_bind_vertex_buffers(command_buffer, 0, ::std::array{ result.handle.handle }, ::std::array{ ::VkDeviceSize{} });
	::vkCmdDraw(command_buffer, static_cast<::std::uint32_t>(vertices.size()), 1, 0, 0);
	return result;
}
