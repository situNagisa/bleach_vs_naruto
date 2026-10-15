#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

#include <vkfu/vulkan/v1.4.328.h>
#include <vkkl/vkkl.h>

#include "../detail/demo_vulkan.h"

#include "./geometry.h"

struct vector_graph_graphic_pipeline_type
{
	::vkkl::pipeline_layout layout;
	::vkkl::pipeline pipeline;

	vector_graph_graphic_pipeline_type(VkDevice device, VkFormat format)
		: layout(device, ::vkfu::create_pipeline_layout(device, ::vkfu::param::pipeline_layout{
				.set_layouts{}
			}))
		, pipeline([&]
			{
				namespace param = ::vkfu::param;
				using namespace ::vkfu::enums;

				auto shader = [&](::std::span<::std::uint32_t const> code)
					{
						return ::vkkl::shader_module{ device, ::vkfu::create_shader_module(device, param::shader_module{.code_size = code.size_bytes(), .code = code.data()}) };
					};
				auto resource_dir = ::std::filesystem::path{ __FILE__ }.parent_path();
				auto const vertex_path = resource_dir / "vector.vert.spv";
				auto const fragment_path = resource_dir / "vector.frag.spv";
				auto vertex = shader(::consumer_arch_vulkan::read_spirv(vertex_path.string().c_str()));
				auto fragment = shader(::consumer_arch_vulkan::read_spirv(fragment_path.string().c_str()));
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
							},
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
	{
	}
};

