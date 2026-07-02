#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <glm/glm.hpp>

#include <stdexec/execution.hpp>
#include <nagisa/concurrency/coroutine.h>

#include <bvn/display_architecture/renderable.h>
#include <bvn/gameplay/entity.h>
#include <bvn/renderer/camera.h>
#include <bvn/renderer/vulkan_renderer.h>
#include <vkkl/vkkl.h>

#include "context.h"
#include "preview_state.h"

namespace
{
	struct arena
	{
		struct vertex
		{
			::glm::vec3 position = {};
			::glm::vec3 color = {};
		};

		struct push_constants
		{
			::glm::mat4 view_projection = ::glm::mat4{1.0f};
		};

		auto main(context& game_context) -> ::bvn::gameplay::task
		{
			owner = &game_context.renderer;
			render_workflow_scheduler = ::stdexec::get_scheduler(game_context.render_workflow);
			if (auto existing = game_context.registry.ctx().find<preview_state>(); existing != nullptr)
			{
				preview = existing;
			}
			else
			{
				preview = &game_context.registry.ctx().emplace<preview_state>();
			}
			game_context.render_scope.spawn(::stdexec::starts_on(::stdexec::get_scheduler(game_context.render_workflow), ::bvn::display_architecture::render(*this, game_context.renderer)));
			co_return;
		}

		auto render(::bvn::renderer::vulkan_renderer& renderer) -> ::bvn::gameplay::task
		{
			assert(owner == &renderer);
			auto env = co_await ::nagisa::concurrency::environment();
			auto stop = ::stdexec::get_stop_token(env);

			auto vertices = ::std::vector<vertex>{};
			vertices.reserve(84);

			for (auto index = -10; index <= 10; ++index)
			{
				auto coordinate = static_cast<float>(index);
				auto major = index == 0;
				auto color_x = major ? ::glm::vec3{0.85f, 0.18f, 0.16f} : ::glm::vec3{0.25f, 0.28f, 0.32f};
				auto color_z = major ? ::glm::vec3{0.20f, 0.62f, 0.24f} : ::glm::vec3{0.25f, 0.28f, 0.32f};

				vertices.push_back({.position = {-10.0f, 0.0f, coordinate}, .color = color_x});
				vertices.push_back({.position = {10.0f, 0.0f, coordinate}, .color = color_x});
				vertices.push_back({.position = {coordinate, 0.0f, -10.0f}, .color = color_z});
				vertices.push_back({.position = {coordinate, 0.0f, 10.0f}, .color = color_z});
			}

			auto vertex_count = static_cast<::std::uint32_t>(vertices.size());
			auto vertex_memory = ::vkkl::device_memory{};
			auto vertex_buffer = ::vkkl::buffer{};

			//+ create grid vertex buffer
			{
				auto buffer_info = VkBufferCreateInfo{};
				buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				buffer_info.size = static_cast<VkDeviceSize>(vertices.size() * sizeof(vertex));
				buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
				buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

				auto raw_buffer = VkBuffer{};
				if (::vkCreateBuffer(renderer.device.handle, &buffer_info, nullptr, &raw_buffer) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create arena vertex buffer"};
				}
				vertex_buffer = ::vkkl::buffer{renderer.device.handle, raw_buffer};

				auto requirements = VkMemoryRequirements{};
				::vkGetBufferMemoryRequirements(renderer.device.handle, vertex_buffer.handle, &requirements);

				auto allocate_info = VkMemoryAllocateInfo{};
				allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				allocate_info.allocationSize = requirements.size;
				auto memory_properties = VkPhysicalDeviceMemoryProperties{};
				::vkGetPhysicalDeviceMemoryProperties(renderer.physical_device, &memory_properties);
				auto found_memory_type = false;

				for (auto index = ::std::uint32_t{}; index < memory_properties.memoryTypeCount; ++index)
				{
					if ((requirements.memoryTypeBits & (1u << index)) != 0
						&& (memory_properties.memoryTypes[index].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
					{
						allocate_info.memoryTypeIndex = index;
						found_memory_type = true;
						break;
					}
				}

				if (!found_memory_type)
				{
					throw ::std::runtime_error{"failed to find arena vertex buffer memory type"};
				}

				auto raw_memory = VkDeviceMemory{};
				if (::vkAllocateMemory(renderer.device.handle, &allocate_info, nullptr, &raw_memory) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to allocate arena vertex buffer memory"};
				}
				vertex_memory = ::vkkl::device_memory{renderer.device.handle, raw_memory};

				if (::vkBindBufferMemory(renderer.device.handle, vertex_buffer.handle, vertex_memory.handle, 0) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to bind arena vertex buffer memory"};
				}

				auto mapped = static_cast<void*>(nullptr);
				if (::vkMapMemory(renderer.device.handle, vertex_memory.handle, 0, buffer_info.size, 0, &mapped) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to map arena vertex buffer"};
				}
				::std::memcpy(mapped, vertices.data(), static_cast<::std::size_t>(buffer_info.size));
				::vkUnmapMemory(renderer.device.handle, vertex_memory.handle);
			}

			auto vertex_shader_words = ::std::vector<::std::uint32_t>{};
			auto fragment_shader_words = ::std::vector<::std::uint32_t>{};

			//+ read arena vertex shader SPIR-V
			{
				auto file = ::std::ifstream{BVN_GRID_VERT_SPV, ::std::ios::binary | ::std::ios::ate};
				if (!file)
				{
					throw ::std::runtime_error{"failed to open arena vertex shader"};
				}

				auto size = file.tellg();
				if (size <= 0 || size % static_cast<::std::streamoff>(sizeof(::std::uint32_t)) != 0)
				{
					throw ::std::runtime_error{"arena vertex shader is not valid SPIR-V"};
				}

				vertex_shader_words.resize(static_cast<::std::size_t>(size) / sizeof(::std::uint32_t));
				file.seekg(0, ::std::ios::beg);
				file.read(reinterpret_cast<char*>(vertex_shader_words.data()), size);
			}

			//+ read arena fragment shader SPIR-V
			{
				auto file = ::std::ifstream{BVN_GRID_FRAG_SPV, ::std::ios::binary | ::std::ios::ate};
				if (!file)
				{
					throw ::std::runtime_error{"failed to open arena fragment shader"};
				}

				auto size = file.tellg();
				if (size <= 0 || size % static_cast<::std::streamoff>(sizeof(::std::uint32_t)) != 0)
				{
					throw ::std::runtime_error{"arena fragment shader is not valid SPIR-V"};
				}

				fragment_shader_words.resize(static_cast<::std::size_t>(size) / sizeof(::std::uint32_t));
				file.seekg(0, ::std::ios::beg);
				file.read(reinterpret_cast<char*>(fragment_shader_words.data()), size);
			}

			auto vertex_shader = ::vkkl::shader_module{};
			auto fragment_shader = ::vkkl::shader_module{};
			auto pipeline_layout = ::vkkl::pipeline_layout{};
			auto pipeline = ::vkkl::pipeline{};

			//+ create grid pipeline
			{
				auto shader_info = VkShaderModuleCreateInfo{};
				shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				shader_info.codeSize = vertex_shader_words.size() * sizeof(::std::uint32_t);
				shader_info.pCode = vertex_shader_words.data();

				auto raw_vertex_shader = VkShaderModule{};
				if (::vkCreateShaderModule(renderer.device.handle, &shader_info, nullptr, &raw_vertex_shader) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create arena vertex shader module"};
				}
				vertex_shader = ::vkkl::shader_module{renderer.device.handle, raw_vertex_shader};

				shader_info.codeSize = fragment_shader_words.size() * sizeof(::std::uint32_t);
				shader_info.pCode = fragment_shader_words.data();

				auto raw_fragment_shader = VkShaderModule{};
				if (::vkCreateShaderModule(renderer.device.handle, &shader_info, nullptr, &raw_fragment_shader) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create arena fragment shader module"};
				}
				fragment_shader = ::vkkl::shader_module{renderer.device.handle, raw_fragment_shader};

				auto push_range = VkPushConstantRange{};
				push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
				push_range.offset = 0;
				push_range.size = sizeof(push_constants);

				auto layout_info = VkPipelineLayoutCreateInfo{};
				layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				layout_info.pushConstantRangeCount = 1;
				layout_info.pPushConstantRanges = &push_range;

				auto raw_pipeline_layout = VkPipelineLayout{};
				if (::vkCreatePipelineLayout(renderer.device.handle, &layout_info, nullptr, &raw_pipeline_layout) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create arena pipeline layout"};
				}
				pipeline_layout = ::vkkl::pipeline_layout{renderer.device.handle, raw_pipeline_layout};

				auto binding = VkVertexInputBindingDescription{};
				binding.binding = 0;
				binding.stride = sizeof(vertex);
				binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

				auto attributes = ::std::array<VkVertexInputAttributeDescription, 2>
				{
					VkVertexInputAttributeDescription{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vertex, position)},
					VkVertexInputAttributeDescription{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vertex, color)},
				};

				auto vertex_input = VkPipelineVertexInputStateCreateInfo{};
				vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
				vertex_input.vertexBindingDescriptionCount = 1;
				vertex_input.pVertexBindingDescriptions = &binding;
				vertex_input.vertexAttributeDescriptionCount = static_cast<::std::uint32_t>(attributes.size());
				vertex_input.pVertexAttributeDescriptions = attributes.data();

				auto input_assembly = VkPipelineInputAssemblyStateCreateInfo{};
				input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
				input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

				auto blend = VkPipelineColorBlendAttachmentState{};
				blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

				auto depth_stencil = VkPipelineDepthStencilStateCreateInfo{};
				depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
				depth_stencil.depthTestEnable = VK_TRUE;
				depth_stencil.depthWriteEnable = VK_TRUE;
				depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

				auto vertex_stage = VkPipelineShaderStageCreateInfo{};
				vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
				vertex_stage.module = vertex_shader.handle;
				vertex_stage.pName = "main";

				auto fragment_stage = VkPipelineShaderStageCreateInfo{};
				fragment_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				fragment_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
				fragment_stage.module = fragment_shader.handle;
				fragment_stage.pName = "main";

				auto stages = ::std::array{vertex_stage, fragment_stage};

				auto color_format = renderer.swapchain_image_format;
				auto rendering_info = VkPipelineRenderingCreateInfo{};
				rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
				rendering_info.colorAttachmentCount = 1;
				rendering_info.pColorAttachmentFormats = &color_format;
				rendering_info.depthAttachmentFormat = renderer.depth_format;

				auto dynamic_states = ::std::array{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
				auto dynamic_state = VkPipelineDynamicStateCreateInfo{};
				dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
				dynamic_state.dynamicStateCount = static_cast<::std::uint32_t>(dynamic_states.size());
				dynamic_state.pDynamicStates = dynamic_states.data();

				auto viewport_state = VkPipelineViewportStateCreateInfo{};
				viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
				viewport_state.viewportCount = 1;
				viewport_state.scissorCount = 1;

				auto rasterization = VkPipelineRasterizationStateCreateInfo{};
				rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
				rasterization.polygonMode = VK_POLYGON_MODE_FILL;
				rasterization.cullMode = VK_CULL_MODE_NONE;
				rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
				rasterization.lineWidth = 1.0f;

				auto multisample = VkPipelineMultisampleStateCreateInfo{};
				multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
				multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

				auto color_blend = VkPipelineColorBlendStateCreateInfo{};
				color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				color_blend.attachmentCount = 1;
				color_blend.pAttachments = &blend;

				auto pipeline_info = VkGraphicsPipelineCreateInfo{};
				pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				pipeline_info.pNext = &rendering_info;
				pipeline_info.stageCount = static_cast<::std::uint32_t>(stages.size());
				pipeline_info.pStages = stages.data();
				pipeline_info.pVertexInputState = &vertex_input;
				pipeline_info.pInputAssemblyState = &input_assembly;
				pipeline_info.pViewportState = &viewport_state;
				pipeline_info.pRasterizationState = &rasterization;
				pipeline_info.pMultisampleState = &multisample;
				pipeline_info.pDepthStencilState = &depth_stencil;
				pipeline_info.pColorBlendState = &color_blend;
				pipeline_info.pDynamicState = &dynamic_state;
				pipeline_info.layout = pipeline_layout.handle;

				auto raw_pipeline = VkPipeline{};
				if (::vkCreateGraphicsPipelines(renderer.device.handle, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &raw_pipeline) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create arena pipeline"};
				}
				pipeline = ::vkkl::pipeline{renderer.device.handle, raw_pipeline};
			}

			try
			{
				while (!stop.stop_requested())
				{
					co_await ::stdexec::schedule(render_workflow_scheduler);

					if (stop.stop_requested())
					{
						break;
					}

					auto camera = ::bvn::renderer::camera{};
					{
						auto lock = ::std::lock_guard{preview->data_mutex};
						camera = preview->view_camera;
					}

					auto command = renderer.command_buffer;
					auto push = push_constants{.view_projection = camera.projection * camera.view};
					auto offset = VkDeviceSize{};

					auto viewport = VkViewport{};
					viewport.x = 0.0f;
					viewport.y = 0.0f;
					viewport.width = static_cast<float>(renderer.swapchain_extent.width);
					viewport.height = static_cast<float>(renderer.swapchain_extent.height);
					viewport.minDepth = 0.0f;
					viewport.maxDepth = 1.0f;

					auto scissor = VkRect2D{};
					scissor.offset = {0, 0};
					scissor.extent = renderer.swapchain_extent;

					::vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);
					::vkCmdSetViewport(command, 0, 1, &viewport);
					::vkCmdSetScissor(command, 0, 1, &scissor);
					::vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer.handle, &offset);
					::vkCmdPushConstants(command, pipeline_layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
					::vkCmdDraw(command, vertex_count, 1, 0, 0);
				}
			}
			catch (...)
			{
				if (renderer.device.handle != VK_NULL_HANDLE)
				{
					(void)::vkDeviceWaitIdle(renderer.device.handle);
				}

				throw;
			}

			if (renderer.device.handle != VK_NULL_HANDLE)
			{
				(void)::vkDeviceWaitIdle(renderer.device.handle);
			}
		}

			preview_state* preview = nullptr;
			::bvn::renderer::vulkan_renderer* owner = nullptr;
			render_workflow::scheduler render_workflow_scheduler{};
	};

	static_assert(::bvn::display_architecture::renderable<arena&, ::bvn::renderer::vulkan_renderer&>);
}
