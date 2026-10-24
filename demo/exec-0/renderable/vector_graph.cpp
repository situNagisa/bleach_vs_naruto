module;

#include <cassert>
#include <cmath>
#include <vkfu/vulkan/v1.4.328/version_1_0.h>
#include <vkfu/vulkan/v1.4.328/version_1_1.h>
#include <vkfu/vulkan/v1.4.328/version_1_3.h>
#include <vkkl/buffer.h>
#include <vkkl/device_memory.h>
#include <vkkl/pipeline.h>
#include <vkkl/pipeline_layout.h>
#include <vkkl/shader_module.h>
#include <glm/glm.hpp>

module demo.renderable.vector_graph;

namespace
{
struct vertex_type
{
	vector_graph::point position;
	vector_graph::color color;
};

constexpr auto vertex_code = ::std::array{
	0x07230203u, 0x00010600u, 0x0008000bu, 0x0000001fu, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
	0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
	0x0009000fu, 0x00000000u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000du, 0x00000012u, 0x0000001bu,
	0x0000001du, 0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u,
	0x00060005u, 0x0000000bu, 0x505f6c67u, 0x65567265u, 0x78657472u, 0x00000000u, 0x00060006u, 0x0000000bu,
	0x00000000u, 0x505f6c67u, 0x7469736fu, 0x006e6f69u, 0x00070006u, 0x0000000bu, 0x00000001u, 0x505f6c67u,
	0x746e696fu, 0x657a6953u, 0x00000000u, 0x00070006u, 0x0000000bu, 0x00000002u, 0x435f6c67u, 0x4470696cu,
	0x61747369u, 0x0065636eu, 0x00070006u, 0x0000000bu, 0x00000003u, 0x435f6c67u, 0x446c6c75u, 0x61747369u,
	0x0065636eu, 0x00030005u, 0x0000000du, 0x00000000u, 0x00050005u, 0x00000012u, 0x69736f70u, 0x6e6f6974u,
	0x00000000u, 0x00060005u, 0x0000001bu, 0x74726576u, 0x635f7865u, 0x726f6c6fu, 0x00000000u, 0x00040005u,
	0x0000001du, 0x6f6c6f63u, 0x00000072u, 0x00030047u, 0x0000000bu, 0x00000002u, 0x00050048u, 0x0000000bu,
	0x00000000u, 0x0000000bu, 0x00000000u, 0x00050048u, 0x0000000bu, 0x00000001u, 0x0000000bu, 0x00000001u,
	0x00050048u, 0x0000000bu, 0x00000002u, 0x0000000bu, 0x00000003u, 0x00050048u, 0x0000000bu, 0x00000003u,
	0x0000000bu, 0x00000004u, 0x00040047u, 0x00000012u, 0x0000001eu, 0x00000000u, 0x00040047u, 0x0000001bu,
	0x0000001eu, 0x00000000u, 0x00040047u, 0x0000001du, 0x0000001eu, 0x00000001u, 0x00020013u, 0x00000002u,
	0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u,
	0x00000006u, 0x00000004u, 0x00040015u, 0x00000008u, 0x00000020u, 0x00000000u, 0x0004002bu, 0x00000008u,
	0x00000009u, 0x00000001u, 0x0004001cu, 0x0000000au, 0x00000006u, 0x00000009u, 0x0006001eu, 0x0000000bu,
	0x00000007u, 0x00000006u, 0x0000000au, 0x0000000au, 0x00040020u, 0x0000000cu, 0x00000003u, 0x0000000bu,
	0x0004003bu, 0x0000000cu, 0x0000000du, 0x00000003u, 0x00040015u, 0x0000000eu, 0x00000020u, 0x00000001u,
	0x0004002bu, 0x0000000eu, 0x0000000fu, 0x00000000u, 0x00040017u, 0x00000010u, 0x00000006u, 0x00000002u,
	0x00040020u, 0x00000011u, 0x00000001u, 0x00000010u, 0x0004003bu, 0x00000011u, 0x00000012u, 0x00000001u,
	0x0004002bu, 0x00000006u, 0x00000014u, 0x00000000u, 0x0004002bu, 0x00000006u, 0x00000015u, 0x3f800000u,
	0x00040020u, 0x00000019u, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000019u, 0x0000001bu, 0x00000003u,
	0x00040020u, 0x0000001cu, 0x00000001u, 0x00000007u, 0x0004003bu, 0x0000001cu, 0x0000001du, 0x00000001u,
	0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003du,
	0x00000010u, 0x00000013u, 0x00000012u, 0x00050051u, 0x00000006u, 0x00000016u, 0x00000013u, 0x00000000u,
	0x00050051u, 0x00000006u, 0x00000017u, 0x00000013u, 0x00000001u, 0x00070050u, 0x00000007u, 0x00000018u,
	0x00000016u, 0x00000017u, 0x00000014u, 0x00000015u, 0x00050041u, 0x00000019u, 0x0000001au, 0x0000000du,
	0x0000000fu, 0x0003003eu, 0x0000001au, 0x00000018u, 0x0004003du, 0x00000007u, 0x0000001eu, 0x0000001du,
	0x0003003eu, 0x0000001bu, 0x0000001eu, 0x000100fdu, 0x00010038u,
};

constexpr auto fragment_code = ::std::array{
	0x07230203u, 0x00010600u, 0x0008000bu, 0x0000000du, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
	0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
	0x0007000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00000009u, 0x0000000bu, 0x00030010u,
	0x00000004u, 0x00000007u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u, 0x6e69616du,
	0x00000000u, 0x00060005u, 0x00000009u, 0x7074756fu, 0x635f7475u, 0x726f6c6fu, 0x00000000u, 0x00060005u,
	0x0000000bu, 0x74726576u, 0x635f7865u, 0x726f6c6fu, 0x00000000u, 0x00040047u, 0x00000009u, 0x0000001eu,
	0x00000000u, 0x00040047u, 0x0000000bu, 0x0000001eu, 0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u,
	0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u,
	0x00000004u, 0x00040020u, 0x00000008u, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u,
	0x00000003u, 0x00040020u, 0x0000000au, 0x00000001u, 0x00000007u, 0x0004003bu, 0x0000000au, 0x0000000bu,
	0x00000001u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u,
	0x0004003du, 0x00000007u, 0x0000000cu, 0x0000000bu, 0x0003003eu, 0x00000009u, 0x0000000cu, 0x000100fdu,
	0x00010038u,
};

}

struct vector_graph::impl
{
	::VkDevice device;
	::vkkl::pipeline_layout layout;
	::vkkl::pipeline pipeline;

	impl(::VkDevice device, ::VkFormat format)
		: device(device)
		, layout(device, ::vkfu::create_pipeline_layout(device, ::vkfu::param::pipeline_layout{}))
		, pipeline([&]
			{
				namespace param = ::vkfu::param;
				using namespace ::vkfu::enums;

				auto shader = [&](::std::span<::std::uint32_t const> code)
					{
						return ::vkkl::shader_module{ device, ::vkfu::create_shader_module(device, param::shader_module{.code_size = code.size_bytes(), .code = code.data()}) };
					};
				auto vertex = shader(vertex_code);
				auto fragment = shader(fragment_code);
				auto stages = ::std::array{
					::vkfu::evaluate(param::state::shader_stage{.stage = shader_stage::vertex, .module = vertex.handle, .name = "main"}),
					::vkfu::evaluate(param::state::shader_stage{.stage = shader_stage::fragment, .module = fragment.handle, .name = "main"})
				};
				auto attachment = ::vkfu::evaluate(param::state::color_blend_attachment{
					.blend_enable = true,
					.src_color_blend_factor = blend_factor::src_alpha,
					.dst_color_blend_factor = blend_factor::one_minus_src_alpha,
					.src_alpha_blend_factor = blend_factor::one,
					.dst_alpha_blend_factor = blend_factor::one_minus_src_alpha,
					.color_write_mask = {.r = 1, .g = 1, .b = 1, .a = 1}
					});
				return ::vkkl::pipeline{ device,
					::vkfu::create_graphics_pipeline(device, VK_NULL_HANDLE, param::graphics_pipeline{
						.stage_count = static_cast<::std::uint32_t>(stages.size()),
						.stages = stages.data(),
						.vertex_input_state = param::state::vertex_input{
							.vertex_binding_descriptions = ::std::array{ ::vkfu::evaluate(param::vertex_input_binding_description{.stride = sizeof(vertex_type), .input_rate = vertex_input_rate::vertex }) },
							.vertex_attribute_descriptions = ::std::array{
								::vkfu::evaluate(param::vertex_input_attribute_description{.location = 0, .format = format::r32g32_sfloat, .offset = offsetof(vertex_type, position)}),

								::vkfu::evaluate(param::vertex_input_attribute_description{.location = 1, .format = format::r32g32b32a32_sfloat, .offset = offsetof(vertex_type, color)})
							}
						},
						.input_assembly_state = param::state::input_assembly{.topology = primitive_topology::triangle_list},
						.viewport_state = param::state::viewport{.viewport_count = 1, .scissor_count = 1},
						.rasterization_state = param::state::rasterization{
							.polygon_mode = polygon_mode::fill,
							.front_face = front_face::counter_clockwise,
							.line_width = 1.0f
						},
						.multisample_state = param::state::multisample{.rasterization_samples = sample_count::count_1},
						.color_blend_state = param::state::color_blend{.attachment_count = 1, .attachments = &attachment},
						.dynamic_state = param::state::dynamic{.states = ::std::array{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }},
						.layout = layout.handle,
					} | param::option::pipeline_rendering{.color_attachment_formats = ::std::array{ format } }
					) };
			}())
	{}
};

vector_graph::vector_graph(::VkDevice device, ::VkFormat format)
	: _impl(::std::make_unique<impl>(device, format)) {}
vector_graph::~vector_graph() = default;
vector_graph::vector_graph(vector_graph&&) noexcept = default;
vector_graph& vector_graph::operator=(vector_graph&&) noexcept= default;
buffer_type vector_graph::draw_rectangle(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkCommandBuffer command_buffer, point size, color fill, ::glm::mat3x3 transform) const
{
	assert(_impl && device == _impl->device);
	assert(::std::isfinite(size.x) && ::std::isfinite(size.y) && size.x >= 0.0f && size.y >= 0.0f);
	auto result = buffer_type{};
	if (size.x == 0.0f || size.y == 0.0f)
		return result;
	auto transform_point = [&](point position)
	{
		auto const transformed = transform * ::glm::vec3{ position.x, position.y, 1.0f };
		return point{ transformed.x, transformed.y };
	};
	auto const p0 = transform_point({0.0f, 0.0f});
	auto const p1 = transform_point({size.x, 0.0f});
	auto const p2 = transform_point(size);
	auto const p3 = transform_point({0.0f, size.y});
	auto const vertices = ::std::array{
		vertex_type{p0, fill}, vertex_type{p1, fill}, vertex_type{p2, fill},
		vertex_type{p0, fill}, vertex_type{p2, fill}, vertex_type{p3, fill}
	};
	result = buffer_type{ device, physical_device, ::std::as_bytes(::std::span{ vertices }), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };
	::vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _impl->pipeline.handle);
	::vkfu::cmd_bind_vertex_buffers(command_buffer, 0,
		::std::array{ result.handle() }, ::std::array{ ::VkDeviceSize{} });
	::vkCmdDraw(command_buffer, static_cast<::std::uint32_t>(vertices.size()), 1, 0, 0);
	return result;
}
