#pragma once

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <nagisa/concurrency/coroutine.h>
#include <stdexec/execution.hpp>

#include <bvn/assets/resource_cache.h>
#include <bvn/assets/sprite_clip.h>
#include <bvn/graphics/render_workflow.h>
#include <bvn/graphics/renderable.h>
#include <bvn/graphics/vulkan_renderer.h>
#include <bvn/gameplay/entity.h>
#include <bvn/renderer/camera.h>
#include <vkkl/vkkl.h>

#include "context.h"
#include "preview_state.h"

namespace
{
enum class hero_action : ::std::size_t
{
	idle = 0,
	walk = 1,
	run = 2,
	count,
};

inline constexpr auto hero_action_count = static_cast<::std::size_t>(hero_action::count);

struct hero
{
	struct sprite_clip_frame
	{
		::glm::vec3 position = {};
		bool facing_right = true;
		float speed = 0.0f;
		::std::uint64_t animation_tick = 0;
		hero_action action = hero_action::idle;
	};

	struct vertex
	{
		::glm::vec3 position = {};
		::glm::vec2 uv = {};
	};

	struct push_constants
	{
		::glm::mat4 view_projection = ::glm::mat4{1.0f};
		::glm::vec4 uv_rect = {0.0f, 0.0f, 1.0f, 1.0f};
	};

	struct clip_resource
	{
		::bvn::assets::asset_handle<::bvn::assets::sprite_clip_data> source;
		::std::uint32_t frame_width = 0;
		::std::uint32_t frame_height = 0;
		::std::uint32_t frame_count = 0;
		::std::uint64_t uploaded_revision = 0;
	};

	struct clip_texture_resource
	{
		::vkkl::device_memory image_memory;
		::vkkl::image image;
		::vkkl::image_view image_view;
		::vkkl::descriptor_set descriptor_set;
		::std::uint32_t atlas_width = 0;
		::std::uint32_t atlas_height = 0;
	};

	auto main(context &game_context) -> ::bvn::gameplay::task
	{
		render_workflow = &game_context.render_workflow;
		if (auto existing = game_context.registry.ctx().find<preview_state>(); existing != nullptr)
		{
			preview = existing;
		}
		else
		{
			preview = &game_context.registry.ctx().emplace<preview_state>();
		}
		asset_root = ::std::filesystem::path{BVN_ASSET_ROOT};
		clips[static_cast<::std::size_t>(hero_action::idle)].source = sprite_clips.load(asset_root / "source/stand.gif");
		clips[static_cast<::std::size_t>(hero_action::walk)].source = sprite_clips.load(asset_root / "source/walk.gif");
		clips[static_cast<::std::size_t>(hero_action::run)].source = sprite_clips.load(asset_root / "source/run.gif");
		game_context.render_scope.spawn(::stdexec::starts_on(game_context.render_workflow.get_scheduler(), ::bvn::graphics::render(*this, ::bvn::graphics::dynamic_forward_global_env_renderer(game_context.renderer.global_env()))));
		co_return;
	}

	auto render(::bvn::graphics::global_dynamic_forward_env_renderer global) -> ::bvn::gameplay::task
	{
		auto env = co_await ::nagisa::concurrency::environment();
		auto stop = ::stdexec::get_stop_token(env);
		auto scheduler = ::stdexec::get_scheduler(env);

		sprite_clips.poll();

		auto descriptor_set_layout = ::vkkl::descriptor_set_layout{};
		auto descriptor_pool = ::vkkl::descriptor_pool{};
		auto texture_sampler = ::vkkl::sampler{};
		auto clip_textures = ::std::array<clip_texture_resource, hero_action_count>{};
		auto upload_command_pool = ::vkkl::command_pool{};
		auto upload_command_buffer = ::vkkl::command_buffer{};
		auto pipeline_layout = ::vkkl::pipeline_layout{};
		auto vertex_memory = ::vkkl::device_memory{};
		auto vertex_buffer = ::vkkl::buffer{};
		auto vertex_count = 6u;

		//+ create persistent sprite draw resources
		{
			auto sampler_binding = VkDescriptorSetLayoutBinding{};
			sampler_binding.binding = 0;
			sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			sampler_binding.descriptorCount = 1;
			sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			auto descriptor_layout_info = VkDescriptorSetLayoutCreateInfo{};
			descriptor_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptor_layout_info.bindingCount = 1;
			descriptor_layout_info.pBindings = &sampler_binding;

			auto raw_descriptor_set_layout = VkDescriptorSetLayout{};
			if (::vkCreateDescriptorSetLayout(global.device(), &descriptor_layout_info, nullptr, &raw_descriptor_set_layout) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to create hero descriptor set layout"};
			}
			descriptor_set_layout = ::vkkl::descriptor_set_layout{global.device(), raw_descriptor_set_layout};

			auto descriptor_pool_size = VkDescriptorPoolSize{};
			descriptor_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptor_pool_size.descriptorCount = static_cast<::std::uint32_t>(hero_action_count);

			auto descriptor_pool_info = VkDescriptorPoolCreateInfo{};
			descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descriptor_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			descriptor_pool_info.maxSets = static_cast<::std::uint32_t>(hero_action_count);
			descriptor_pool_info.poolSizeCount = 1;
			descriptor_pool_info.pPoolSizes = &descriptor_pool_size;

			auto raw_descriptor_pool = VkDescriptorPool{};
			if (::vkCreateDescriptorPool(global.device(), &descriptor_pool_info, nullptr, &raw_descriptor_pool) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to create hero descriptor pool"};
			}
			descriptor_pool = ::vkkl::descriptor_pool{global.device(), raw_descriptor_pool};

			auto sampler_info = VkSamplerCreateInfo{};
			sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sampler_info.magFilter = VK_FILTER_NEAREST;
			sampler_info.minFilter = VK_FILTER_NEAREST;
			sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler_info.minLod = 0.0f;
			sampler_info.maxLod = 0.0f;
			sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

			auto raw_sampler = VkSampler{};
			if (::vkCreateSampler(global.device(), &sampler_info, nullptr, &raw_sampler) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to create hero texture sampler"};
			}
			texture_sampler = ::vkkl::sampler{global.device(), raw_sampler};

			auto upload_pool_info = VkCommandPoolCreateInfo{};
			upload_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			upload_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			upload_pool_info.queueFamilyIndex = global.graphics_queue_family();

			auto raw_upload_command_pool = VkCommandPool{};
			if (::vkCreateCommandPool(global.device(), &upload_pool_info, nullptr, &raw_upload_command_pool) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to create hero upload command pool"};
			}
			upload_command_pool = ::vkkl::command_pool{global.device(), raw_upload_command_pool};

			auto upload_allocate_info = VkCommandBufferAllocateInfo{};
			upload_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			upload_allocate_info.commandPool = upload_command_pool.handle;
			upload_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			upload_allocate_info.commandBufferCount = 1;

			auto raw_upload_command_buffer = VkCommandBuffer{};
			if (::vkAllocateCommandBuffers(global.device(), &upload_allocate_info, &raw_upload_command_buffer) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to allocate hero upload command buffer"};
			}
			upload_command_buffer = ::vkkl::command_buffer{global.device(), upload_command_pool.handle, raw_upload_command_buffer};

			auto push_range = VkPushConstantRange{};
			push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			push_range.offset = 0;
			push_range.size = sizeof(push_constants);

			auto layout_info = VkPipelineLayoutCreateInfo{};
			layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layout_info.setLayoutCount = 1;
			layout_info.pSetLayouts = &descriptor_set_layout.handle;
			layout_info.pushConstantRangeCount = 1;
			layout_info.pPushConstantRanges = &push_range;

			auto raw_pipeline_layout = VkPipelineLayout{};
			if (::vkCreatePipelineLayout(global.device(), &layout_info, nullptr, &raw_pipeline_layout) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to create hero pipeline layout"};
			}
			pipeline_layout = ::vkkl::pipeline_layout{global.device(), raw_pipeline_layout};

			auto buffer_info = VkBufferCreateInfo{};
			buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			buffer_info.size = static_cast<VkDeviceSize>(vertex_count * sizeof(vertex));
			buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			auto raw_buffer = VkBuffer{};
			if (::vkCreateBuffer(global.device(), &buffer_info, nullptr, &raw_buffer) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to create hero vertex buffer"};
			}
			vertex_buffer = ::vkkl::buffer{global.device(), raw_buffer};

			auto requirements = VkMemoryRequirements{};
			::vkGetBufferMemoryRequirements(global.device(), vertex_buffer.handle, &requirements);

			auto allocate_info = VkMemoryAllocateInfo{};
			allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocate_info.allocationSize = requirements.size;
			auto memory_properties = VkPhysicalDeviceMemoryProperties{};
			::vkGetPhysicalDeviceMemoryProperties(global.physical_device(), &memory_properties);
			auto found_memory_type = false;

			for (auto index = ::std::uint32_t{}; index < memory_properties.memoryTypeCount; ++index)
			{
				if ((requirements.memoryTypeBits & (1u << index)) != 0 && (memory_properties.memoryTypes[index].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
				{
					allocate_info.memoryTypeIndex = index;
					found_memory_type = true;
					break;
				}
			}

			if (!found_memory_type)
			{
				throw ::std::runtime_error{"failed to find hero vertex buffer memory type"};
			}

			auto raw_memory = VkDeviceMemory{};
			if (::vkAllocateMemory(global.device(), &allocate_info, nullptr, &raw_memory) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to allocate hero vertex buffer memory"};
			}
			vertex_memory = ::vkkl::device_memory{global.device(), raw_memory};

			if (::vkBindBufferMemory(global.device(), vertex_buffer.handle, vertex_memory.handle, 0) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to bind hero vertex buffer memory"};
			}

			auto vertices = ::std::array<vertex, 6>{
				vertex{.position = {-0.5f, 1.0f, 0.0f}, .uv = {0.0f, 0.0f}},
				vertex{.position = {-0.5f, 0.0f, 0.0f}, .uv = {0.0f, 1.0f}},
				vertex{.position = {0.5f, 0.0f, 0.0f}, .uv = {1.0f, 1.0f}},
				vertex{.position = {-0.5f, 1.0f, 0.0f}, .uv = {0.0f, 0.0f}},
				vertex{.position = {0.5f, 0.0f, 0.0f}, .uv = {1.0f, 1.0f}},
				vertex{.position = {0.5f, 1.0f, 0.0f}, .uv = {1.0f, 0.0f}},
			};

			auto mapped = static_cast<void*>(nullptr);
			if (::vkMapMemory(global.device(), vertex_memory.handle, 0, static_cast<VkDeviceSize>(vertices.size() * sizeof(vertex)), 0, &mapped) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to map hero vertex buffer"};
			}
			::std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(vertex));
			::vkUnmapMemory(global.device(), vertex_memory.handle);
		}

		auto vertex_shader_words = ::std::vector<::std::uint32_t>{};
		auto fragment_shader_words = ::std::vector<::std::uint32_t>{};

		//+ read hero vertex shader SPIR-V
		{
			auto file = ::std::ifstream{BVN_SPRITE_VERT_SPV, ::std::ios::binary | ::std::ios::ate};
			if (!file)
			{
				throw ::std::runtime_error{"failed to open hero vertex shader"};
			}

			auto size = file.tellg();
			if (size <= 0 || size % static_cast<::std::streamoff>(sizeof(::std::uint32_t)) != 0)
			{
				throw ::std::runtime_error{"hero vertex shader is not valid SPIR-V"};
			}

			vertex_shader_words.resize(static_cast<::std::size_t>(size) / sizeof(::std::uint32_t));
			file.seekg(0, ::std::ios::beg);
			file.read(reinterpret_cast<char*>(vertex_shader_words.data()), size);
		}

		//+ read hero fragment shader SPIR-V
		{
			auto file = ::std::ifstream{BVN_SPRITE_FRAG_SPV, ::std::ios::binary | ::std::ios::ate};
			if (!file)
			{
				throw ::std::runtime_error{"failed to open hero fragment shader"};
			}

			auto size = file.tellg();
			if (size <= 0 || size % static_cast<::std::streamoff>(sizeof(::std::uint32_t)) != 0)
			{
				throw ::std::runtime_error{"hero fragment shader is not valid SPIR-V"};
			}

			fragment_shader_words.resize(static_cast<::std::size_t>(size) / sizeof(::std::uint32_t));
			file.seekg(0, ::std::ios::beg);
			file.read(reinterpret_cast<char*>(fragment_shader_words.data()), size);
		}

		auto vertex_shader = ::vkkl::shader_module{};
		auto fragment_shader = ::vkkl::shader_module{};
		auto pipeline = ::vkkl::pipeline{};

		//+ create sprite graphics pipeline
		{
			auto shader_info = VkShaderModuleCreateInfo{};
			shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			shader_info.codeSize = vertex_shader_words.size() * sizeof(::std::uint32_t);
			shader_info.pCode = vertex_shader_words.data();

			auto raw_vertex_shader = VkShaderModule{};
			if (::vkCreateShaderModule(global.device(), &shader_info, nullptr, &raw_vertex_shader) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to create hero vertex shader module"};
			}
			vertex_shader = ::vkkl::shader_module{global.device(), raw_vertex_shader};

			shader_info.codeSize = fragment_shader_words.size() * sizeof(::std::uint32_t);
			shader_info.pCode = fragment_shader_words.data();

			auto raw_fragment_shader = VkShaderModule{};
			if (::vkCreateShaderModule(global.device(), &shader_info, nullptr, &raw_fragment_shader) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to create hero fragment shader module"};
			}
			fragment_shader = ::vkkl::shader_module{global.device(), raw_fragment_shader};

			auto binding = VkVertexInputBindingDescription{};
			binding.binding = 0;
			binding.stride = sizeof(vertex);
			binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			auto attributes = ::std::array<VkVertexInputAttributeDescription, 2>{
				VkVertexInputAttributeDescription{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vertex, position)},
				VkVertexInputAttributeDescription{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vertex, uv)},
			};

			auto vertex_input = VkPipelineVertexInputStateCreateInfo{};
			vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertex_input.vertexBindingDescriptionCount = 1;
			vertex_input.pVertexBindingDescriptions = &binding;
			vertex_input.vertexAttributeDescriptionCount = static_cast<::std::uint32_t>(attributes.size());
			vertex_input.pVertexAttributeDescriptions = attributes.data();

			auto input_assembly = VkPipelineInputAssemblyStateCreateInfo{};
			input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			auto blend = VkPipelineColorBlendAttachmentState{};
			blend.blendEnable = VK_TRUE;
			blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blend.colorBlendOp = VK_BLEND_OP_ADD;
			blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blend.alphaBlendOp = VK_BLEND_OP_ADD;
			blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			auto depth_stencil = VkPipelineDepthStencilStateCreateInfo{};
			depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depth_stencil.depthTestEnable = VK_FALSE;
			depth_stencil.depthWriteEnable = VK_FALSE;
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

			auto color_format = global.swapchain_image_format();
			auto rendering_info = VkPipelineRenderingCreateInfo{};
			rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
			rendering_info.colorAttachmentCount = 1;
			rendering_info.pColorAttachmentFormats = &color_format;
			rendering_info.depthAttachmentFormat = global.depth_format();

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
			if (::vkCreateGraphicsPipelines(global.device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &raw_pipeline) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to create hero pipeline"};
			}
			pipeline = ::vkkl::pipeline{global.device(), raw_pipeline};
		}

		auto secondary_commands = ::bvn::graphics::secondary_command_pool{global.device(), global.graphics_queue_family()};
		auto retirements = ::stdexec::simple_counting_scope{};
		auto render_error = ::std::exception_ptr{};

		try
		{
			//+ synchronize initial hero sprite textures
			{
				sprite_clips.poll();

				for (auto clip_index = ::std::size_t{}; clip_index < clips.size(); ++clip_index)
				{
					auto &&clip = clips[clip_index];
					auto &&clip_texture = clip_textures[clip_index];

					if (!clip.source->error.empty())
					{
						throw ::std::runtime_error{"failed to load hero sprite clip: " + clip.source->error};
					}

					if (clip.source->revision == clip.uploaded_revision)
					{
						continue;
					}

					auto &&clip_data = clip.source->value;
					auto atlas = ::bvn::assets::pack_horizontal_atlas(clip_data);

					if (clip_texture.descriptor_set.handle == VK_NULL_HANDLE)
					{
						auto descriptor_allocate_info = VkDescriptorSetAllocateInfo{};
						descriptor_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
						descriptor_allocate_info.descriptorPool = descriptor_pool.handle;
						descriptor_allocate_info.descriptorSetCount = 1;
						descriptor_allocate_info.pSetLayouts = &descriptor_set_layout.handle;

						auto raw_descriptor_set = VkDescriptorSet{};
						if (::vkAllocateDescriptorSets(global.device(), &descriptor_allocate_info, &raw_descriptor_set) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to allocate hero texture descriptor set"};
						}
						clip_texture.descriptor_set = ::vkkl::descriptor_set{global.device(), descriptor_pool.handle, raw_descriptor_set};
					}

					auto staging_memory = ::vkkl::device_memory{};
					auto staging_buffer = ::vkkl::buffer{};
					auto next_image_memory = ::vkkl::device_memory{};
					auto next_image = ::vkkl::image{};
					auto next_image_view = ::vkkl::image_view{};

					//+ upload sprite atlas into a sampled image
					{
						auto upload_size = static_cast<VkDeviceSize>(atlas.pixels.size());

						auto staging_buffer_info = VkBufferCreateInfo{};
						staging_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
						staging_buffer_info.size = upload_size;
						staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
						staging_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

						auto raw_staging_buffer = VkBuffer{};
						if (::vkCreateBuffer(global.device(), &staging_buffer_info, nullptr, &raw_staging_buffer) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to create hero texture staging buffer"};
						}
						staging_buffer = ::vkkl::buffer{global.device(), raw_staging_buffer};

						auto requirements = VkMemoryRequirements{};
						::vkGetBufferMemoryRequirements(global.device(), staging_buffer.handle, &requirements);

						auto allocate_info = VkMemoryAllocateInfo{};
						allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
						allocate_info.allocationSize = requirements.size;

						auto memory_properties = VkPhysicalDeviceMemoryProperties{};
						::vkGetPhysicalDeviceMemoryProperties(global.physical_device(), &memory_properties);
						auto found_memory_type = false;

						for (auto index = ::std::uint32_t{}; index < memory_properties.memoryTypeCount; ++index)
						{
							if ((requirements.memoryTypeBits & (1u << index)) != 0 && (memory_properties.memoryTypes[index].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
							{
								allocate_info.memoryTypeIndex = index;
								found_memory_type = true;
								break;
							}
						}

						if (!found_memory_type)
						{
							throw ::std::runtime_error{"failed to find hero texture staging memory type"};
						}

						auto raw_staging_memory = VkDeviceMemory{};
						if (::vkAllocateMemory(global.device(), &allocate_info, nullptr, &raw_staging_memory) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to allocate hero texture staging memory"};
						}
						staging_memory = ::vkkl::device_memory{global.device(), raw_staging_memory};

						if (::vkBindBufferMemory(global.device(), staging_buffer.handle, staging_memory.handle, 0) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to bind hero texture staging memory"};
						}

						auto mapped = static_cast<void*>(nullptr);
						if (::vkMapMemory(global.device(), staging_memory.handle, 0, upload_size, 0, &mapped) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to map hero texture staging memory"};
						}
						::std::memcpy(mapped, atlas.pixels.data(), atlas.pixels.size());
						::vkUnmapMemory(global.device(), staging_memory.handle);

						auto image_info = VkImageCreateInfo{};
						image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
						image_info.imageType = VK_IMAGE_TYPE_2D;
						image_info.extent = {atlas.width, atlas.height, 1};
						image_info.mipLevels = 1;
						image_info.arrayLayers = 1;
						image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
						image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
						image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
						image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
						image_info.samples = VK_SAMPLE_COUNT_1_BIT;
						image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

						auto raw_image = VkImage{};
						if (::vkCreateImage(global.device(), &image_info, nullptr, &raw_image) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to create hero texture image"};
						}
						next_image = ::vkkl::image{global.device(), raw_image};

						::vkGetImageMemoryRequirements(global.device(), next_image.handle, &requirements);

						allocate_info = {};
						allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
						allocate_info.allocationSize = requirements.size;
						found_memory_type = false;

						for (auto index = ::std::uint32_t{}; index < memory_properties.memoryTypeCount; ++index)
						{
							if ((requirements.memoryTypeBits & (1u << index)) != 0 && (memory_properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
							{
								allocate_info.memoryTypeIndex = index;
								found_memory_type = true;
								break;
							}
						}

						if (!found_memory_type)
						{
							throw ::std::runtime_error{"failed to find hero texture image memory type"};
						}

						auto raw_image_memory = VkDeviceMemory{};
						if (::vkAllocateMemory(global.device(), &allocate_info, nullptr, &raw_image_memory) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to allocate hero texture image memory"};
						}
						next_image_memory = ::vkkl::device_memory{global.device(), raw_image_memory};

						if (::vkBindImageMemory(global.device(), next_image.handle, next_image_memory.handle, 0) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to bind hero texture image memory"};
						}

						auto view_info = VkImageViewCreateInfo{};
						view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
						view_info.image = next_image.handle;
						view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
						view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
						view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						view_info.subresourceRange.baseMipLevel = 0;
						view_info.subresourceRange.levelCount = 1;
						view_info.subresourceRange.baseArrayLayer = 0;
						view_info.subresourceRange.layerCount = 1;

						auto raw_image_view = VkImageView{};
						if (::vkCreateImageView(global.device(), &view_info, nullptr, &raw_image_view) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to create hero texture image view"};
						}
						next_image_view = ::vkkl::image_view{global.device(), raw_image_view};

						if (::vkResetCommandPool(global.device(), upload_command_pool.handle, 0) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to reset hero upload command pool"};
						}

						auto begin_info = VkCommandBufferBeginInfo{};
						begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
						begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
						if (::vkBeginCommandBuffer(upload_command_buffer.handle, &begin_info) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to begin hero upload command buffer"};
						}

						auto barrier = VkImageMemoryBarrier2{};
						barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
						barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
						barrier.srcAccessMask = 0;
						barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
						barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
						barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
						barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
						barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						barrier.image = next_image.handle;
						barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						barrier.subresourceRange.baseMipLevel = 0;
						barrier.subresourceRange.levelCount = 1;
						barrier.subresourceRange.baseArrayLayer = 0;
						barrier.subresourceRange.layerCount = 1;

						auto dependency = VkDependencyInfo{};
						dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
						dependency.imageMemoryBarrierCount = 1;
						dependency.pImageMemoryBarriers = &barrier;
						::vkCmdPipelineBarrier2(upload_command_buffer.handle, &dependency);

						auto copy = VkBufferImageCopy{};
						copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						copy.imageSubresource.mipLevel = 0;
						copy.imageSubresource.baseArrayLayer = 0;
						copy.imageSubresource.layerCount = 1;
						copy.imageExtent = {atlas.width, atlas.height, 1};
						::vkCmdCopyBufferToImage(upload_command_buffer.handle, staging_buffer.handle, next_image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

						barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
						barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
						barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
						barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
						barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
						barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						::vkCmdPipelineBarrier2(upload_command_buffer.handle, &dependency);

						if (::vkEndCommandBuffer(upload_command_buffer.handle) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to end hero upload command buffer"};
						}

						auto command_info = VkCommandBufferSubmitInfo{};
						command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
						command_info.commandBuffer = upload_command_buffer.handle;

						auto submit_info = VkSubmitInfo2{};
						submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
						submit_info.commandBufferInfoCount = 1;
						submit_info.pCommandBufferInfos = &command_info;

						if (::vkQueueSubmit2(global.graphics_queue(), 1, &submit_info, VK_NULL_HANDLE) != ::VK_SUCCESS)
						{
							throw ::std::runtime_error{"failed to submit hero texture upload"};
						}

						if (::vkQueueWaitIdle(global.graphics_queue()) != ::VK_SUCCESS)
						{
							(void)::vkDeviceWaitIdle(global.device());
							throw ::std::runtime_error{"failed to wait for hero texture upload"};
						}
					}

					auto descriptor_image = VkDescriptorImageInfo{};
					descriptor_image.sampler = texture_sampler.handle;
					descriptor_image.imageView = next_image_view.handle;
					descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

					auto descriptor_write = VkWriteDescriptorSet{};
					descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					descriptor_write.dstSet = clip_texture.descriptor_set.handle;
					descriptor_write.dstBinding = 0;
					descriptor_write.descriptorCount = 1;
					descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					descriptor_write.pImageInfo = &descriptor_image;
					::vkUpdateDescriptorSets(global.device(), 1, &descriptor_write, 0, nullptr);

					clip_texture.image_view = ::std::move(next_image_view);
					clip_texture.image = ::std::move(next_image);
					clip_texture.image_memory = ::std::move(next_image_memory);
					clip_texture.atlas_width = atlas.width;
					clip_texture.atlas_height = atlas.height;
					clip.frame_width = clip_data.frame_width;
					clip.frame_height = clip_data.frame_height;
					clip.frame_count = static_cast<::std::uint32_t>(clip_data.frame_count);
					clip.uploaded_revision = clip.source->revision;
				}
			}

			while (!stop.stop_requested())
			{
				auto frame = co_await render_workflow->async_record(secondary_commands);

				if (!frame || stop.stop_requested())
				{
					break;
				}

				auto command_buffer = frame.allocate();
				auto command = command_buffer.get();

				auto color_format = global.swapchain_image_format();
				auto rendering_info = VkCommandBufferInheritanceRenderingInfo{};
				rendering_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
				rendering_info.colorAttachmentCount = 1;
				rendering_info.pColorAttachmentFormats = &color_format;
				rendering_info.depthAttachmentFormat = global.depth_format();
				rendering_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

				auto inheritance_info = VkCommandBufferInheritanceInfo{};
				inheritance_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
				inheritance_info.pNext = &rendering_info;

				auto begin_info = VkCommandBufferBeginInfo{};
				begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
				begin_info.pInheritanceInfo = &inheritance_info;

				if (::vkBeginCommandBuffer(command, &begin_info) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to begin hero secondary command buffer"};
				}

				auto unit = preview_unit_state{};
				{
					auto lock = ::std::lock_guard{preview->data_mutex};
					unit = preview->hero_unit;
				}

				auto speed = ::glm::length(unit.velocity);
				auto action = hero_action::idle;
				if (speed >= run_speed_threshold)
				{
					action = hero_action::run;
				}
				else if (speed >= walk_speed_threshold)
				{
					action = hero_action::walk;
				}

				auto frame_snapshot = sprite_clip_frame{
					.position = unit.position,
					.facing_right = unit.facing_right,
					.speed = speed,
					.animation_tick = unit.simulation_tick,
					.action = action,
				};
				auto&& active = clips[static_cast<::std::size_t>(frame_snapshot.action)];
				auto&& active_texture = clip_textures[static_cast<::std::size_t>(frame_snapshot.action)];

				if (active.frame_count > 0 && active_texture.descriptor_set.handle != VK_NULL_HANDLE)
				{
					//+ record active sprite quad into this task's secondary command buffer
					{
						auto camera = ::bvn::renderer::camera{};
						{
							auto lock = ::std::lock_guard{preview->data_mutex};
							camera = preview->view_camera;
						}

						auto offset = VkDeviceSize{};
						auto world_width = static_cast<float>(active.frame_width) * sprite_world_scale;
						auto world_height = static_cast<float>(active.frame_height) * sprite_world_scale;
						auto facing = frame_snapshot.facing_right ? 1.0f : -1.0f;
						auto model =
							::glm::translate(::glm::mat4{1.0f}, frame_snapshot.position) * ::glm::scale(::glm::mat4{1.0f}, ::glm::vec3{world_width * facing, world_height, 1.0f});
						auto frame_index = static_cast<::std::uint32_t>((frame_snapshot.animation_tick / animation_ticks_per_frame) % static_cast<::std::uint64_t>(active.frame_count));
						auto frame_width_uv = 1.0f / static_cast<float>(active.frame_count);
						auto push = push_constants{
							.view_projection = camera.projection * camera.view * model,
							.uv_rect = {static_cast<float>(frame_index) * frame_width_uv, 0.0f, frame_width_uv, 1.0f},
						};
						auto descriptor_set = active_texture.descriptor_set.handle;
						auto extent = global.swapchain_extent();

						auto viewport = VkViewport{};
						viewport.x = 0.0f;
						viewport.y = 0.0f;
						viewport.width = static_cast<float>(extent.width);
						viewport.height = static_cast<float>(extent.height);
						viewport.minDepth = 0.0f;
						viewport.maxDepth = 1.0f;

						auto scissor = VkRect2D{};
						scissor.offset = {0, 0};
						scissor.extent = extent;

						::vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);
						::vkCmdSetViewport(command, 0, 1, &viewport);
						::vkCmdSetScissor(command, 0, 1, &scissor);
						::vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer.handle, &offset);
						::vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout.handle, 0, 1, &descriptor_set, 0, nullptr);
						::vkCmdPushConstants(command, pipeline_layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
						::vkCmdDraw(command, vertex_count, 1, 0, 0);
					}
				}

				if (::vkEndCommandBuffer(command) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to end hero secondary command buffer"};
				}

				::stdexec::spawn(frame.retire(::std::move(command_buffer)), retirements.get_token());
			}
		}
		catch (...)
		{
			render_error = ::std::current_exception();
		}

		retirements.close();
		co_await ::stdexec::unstoppable(::stdexec::starts_on(scheduler, retirements.join()));
		if (render_error)
		{
			::std::rethrow_exception(render_error);
		}
	}

	static constexpr auto sprite_world_scale = 0.03f;
	static constexpr auto walk_speed_threshold = 0.5f;
	static constexpr auto run_speed_threshold = 5.0f;
	static constexpr auto animation_ticks_per_frame = 4u;

	::std::filesystem::path asset_root;
	::bvn::assets::resource_cache<::bvn::assets::sprite_clip_data> sprite_clips{::bvn::assets::load_sprite_clip};
	preview_state* preview = nullptr;
	::bvn::graphics::render_workflow* render_workflow = nullptr;
	::std::array<clip_resource, hero_action_count> clips;
};

static_assert(::bvn::graphics::renderable<hero &, ::bvn::graphics::global_dynamic_forward_env_renderer>);
} // namespace
