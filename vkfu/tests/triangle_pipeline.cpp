// The graphics pipeline that demo/consumer-arch builds, expressed the way the
// demo expresses it. Keeps the demo's expression under compiler protection even
// though the demo itself needs SDL3 / vk-bootstrap / glslang to link.

#include <array>
#include <cassert>
#include <cstdint>
#include <span>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

namespace param = ::vkfu::param;
using namespace ::vkfu::enums;

int main()
{
	auto const vertex_module = VkShaderModule{VK_NULL_HANDLE};
	auto const fragment_module = VkShaderModule{VK_NULL_HANDLE};
	auto const pipeline_layout = VkPipelineLayout{VK_NULL_HANDLE};
	auto const swapchain_format = VK_FORMAT_B8G8R8A8_SRGB;

	auto const stages = ::std::array{
		::vkfu::evaluate(param::state::shader_stage{
			.stage = shader_stage::vertex,
			.module = vertex_module,
			.name = "main",
		}),
		::vkfu::evaluate(param::state::shader_stage{
			.stage = shader_stage::fragment,
			.module = fragment_module,
			.name = "main",
		}),
	};
	auto blend_attachment = VkPipelineColorBlendAttachmentState{};
	blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
		| VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT
		| VK_COLOR_COMPONENT_A_BIT;
	auto const dynamic_states = ::std::array{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

	auto pipeline_storage = ::vkfu::evaluate(
		param::graphics_pipeline{
			.stage_count = static_cast<::std::uint32_t>(stages.size()),
			.stages = stages.data(),
			.vertex_input_state = param::state::vertex_input{},
			.input_assembly_state = param::state::input_assembly{
				.topology = primitive_topology::triangle_list,
			},
			// Viewport and scissor are dynamic, so the counts stand alone and
			// the arrays stay null.
			.viewport_state = param::state::viewport{
				.viewport_count = 1,
				.scissor_count = 1,
			},
			.rasterization_state = param::state::rasterization{
				.polygon_mode = polygon_mode::fill,
				.front_face = front_face::clockwise,
				.line_width = 1.0f,
			},
			.multisample_state = param::state::multisample{
				.rasterization_samples = sample_count::count_1,
			},
			.color_blend_state = param::state::color_blend{
				.attachment_count = 1,
				.attachments = &blend_attachment,
			},
			.dynamic_state = param::state::dynamic{
				.states = dynamic_states,
			},
			.layout = pipeline_layout,
		}
		| param::option::pipeline_rendering{
			.color_attachment_formats = ::std::span{&swapchain_format, 1u},
		}
	);

	auto&& info = ::vkfu::unpack(pipeline_storage);
	static_assert(::std::same_as<::std::remove_cvref_t<decltype(info)>, VkGraphicsPipelineCreateInfo>);

	assert(info.sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
	assert(info.stageCount == 2);
	assert(info.pStages == stages.data());
	assert(info.pStages[0].stage == VK_SHADER_STAGE_VERTEX_BIT);
	assert(info.pStages[1].stage == VK_SHADER_STAGE_FRAGMENT_BIT);

	// Each state slot points into the storage that owns it.
	assert(info.pVertexInputState != nullptr);
	assert(info.pVertexInputState->sType == VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
	assert(info.pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	assert(info.pViewportState->viewportCount == 1);
	assert(info.pViewportState->pViewports == nullptr);
	assert(info.pViewportState->scissorCount == 1);
	assert(info.pRasterizationState->polygonMode == VK_POLYGON_MODE_FILL);
	assert(info.pRasterizationState->cullMode == VK_CULL_MODE_NONE);
	assert(info.pRasterizationState->frontFace == VK_FRONT_FACE_CLOCKWISE);
	assert(info.pRasterizationState->lineWidth == 1.0f);
	assert(info.pMultisampleState->rasterizationSamples == VK_SAMPLE_COUNT_1_BIT);
	assert(info.pColorBlendState->attachmentCount == 1);
	assert(info.pColorBlendState->pAttachments == &blend_attachment);
	assert(info.pDynamicState->dynamicStateCount == 2);
	assert(info.pDynamicState->pDynamicStates == dynamic_states.data());
	// Unfilled slots stay null.
	assert(info.pTessellationState == nullptr);
	assert(info.pDepthStencilState == nullptr);

	// The pNext feature hangs off the pipeline itself.
	auto&& rendering = ::std::get<1>(pipeline_storage.storages);
	assert(info.pNext == ::vkfu::address(rendering));
	assert(rendering.colorAttachmentCount == 1);
	assert(rendering.pColorAttachmentFormats == &swapchain_format);
	assert(rendering.pNext == nullptr);
}
