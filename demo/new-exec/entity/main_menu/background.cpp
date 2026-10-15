#include <array>
#include <cstdint>
#include <span>
#include <utility>

#include "./background.h"

background::background(
	::bvn::assets::image_rgba8 img
	, VkDevice device
	, VkPhysicalDevice physical_device
	, VkQueue graphics_queue
	, ::std::uint32_t graphics_queue_family
	, VkFormat format
)
	: pipeline(device, format)
	, texture(::std::move(img), device, physical_device, graphics_queue, graphics_queue_family)
{
	auto const image_info = ::vkfu::evaluate(::vkfu::param::descriptor_image{
		.sampler = texture.sampler.handle,
		.image_view = texture.view.handle,
		.image_layout = ::vkfu::enums::image_layout::shader_read_only_optimal }
		);
	::vkfu::update_descriptor_sets(
		device
		, ::std::array{ ::vkfu::evaluate(::vkfu::param::write_descriptor_set{
			.dst_set = pipeline.descriptors[0].handle,
			.descriptor_count = 1,
			.descriptor_type = ::vkfu::enums::descriptor_type::combined_image_sampler,
			.image_info = &image_info }
			) }
		, {}
	);
}

auto background::draw(
	VkDevice device
	, VkPhysicalDevice physical_device
	, VkCommandBuffer command_buffer
	, ::glm::mat3x3 transform
	) const noexcept -> buffer_type
{
	auto const extent = point{
		static_cast<float>(texture.source.width),
		static_cast<float>(texture.source.height)
	};

	auto transform_point = [&](point p) -> point
		{
			auto const result = transform * ::glm::vec3{ p.x, p.y, 1.0f };
			return { result.x, result.y };
		};

	auto const p0 = transform_point({ 0.0f, 0.0f });
	auto const p1 = transform_point({ extent.x, 0.0f });
	auto const p2 = transform_point({ extent.x, extent.y });
	auto const p3 = transform_point({ 0.0f, extent.y });

	geometry g;
	g.texture(p0, p1, p2, p3, { 0.0f, 0.0f }, { 1.0f, 1.0f });

	::vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
	buffer_type result(device, physical_device, ::std::as_bytes(::std::span{ g.vertices }), ::vkfu::flags::buffer_usage{ .vertex_buffer = 1 });
	::vkfu::cmd_bind_descriptor_sets(command_buffer, ::vkfu::enums::pipeline_bind_point::graphics, pipeline.layout.handle, 0, ::std::array{ pipeline.descriptors[0].handle }, {});
	::vkfu::cmd_bind_vertex_buffers(command_buffer, 0, ::std::array{ result.handle.handle }, ::std::array{ ::VkDeviceSize{} });
	::vkCmdDraw(command_buffer, static_cast<::std::uint32_t>(g.vertices.size()), 1, 0, 0);
	return result;
}

