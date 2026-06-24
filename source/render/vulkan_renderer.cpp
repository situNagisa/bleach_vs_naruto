#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include <bvn/display_architecture/compose.h>
#include <bvn/render/vulkan_renderer.h>

namespace bvn::render
{
	namespace
	{
		constexpr auto depth_format = VK_FORMAT_D32_SFLOAT;
		constexpr auto sprite_world_scale = 0.03f;

		struct grid_push_constants
		{
			glm::mat4 view_projection = glm::mat4{1.0f};
		};

		struct sprite_push_constants
		{
			glm::mat4 view_projection = glm::mat4{1.0f};
		};

		void check(VkResult result, char const* message)
		{
			if (result != VK_SUCCESS)
			{
				throw std::runtime_error(std::string{message} + " VkResult=" + std::to_string(static_cast<int>(result)));
			}
		}

		auto read_spirv(char const* path) -> std::vector<std::uint32_t>
		{
			auto file = std::ifstream{path, std::ios::binary | std::ios::ate};

			if (!file)
			{
				throw std::runtime_error{std::string{"failed to open shader: "} + path};
			}

			auto size = file.tellg();

			if (size <= 0 || size % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0)
			{
				throw std::runtime_error{std::string{"shader is not valid SPIR-V: "} + path};
			}

			auto words = std::vector<std::uint32_t>(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
			file.seekg(0, std::ios::beg);
			file.read(reinterpret_cast<char*>(words.data()), size);
			return words;
		}

		auto make_shader_module(VkDevice device, std::vector<std::uint32_t> const& words) -> VkShaderModule
		{
			auto create_info = VkShaderModuleCreateInfo{};
			create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			create_info.codeSize = words.size() * sizeof(std::uint32_t);
			create_info.pCode = words.data();

			auto shader = VkShaderModule{};
			check(vkCreateShaderModule(device, &create_info, nullptr, &shader), "failed to create shader module");
			return shader;
		}

		auto device_address(std::vector<std::byte> const& bytes) noexcept -> void const*
		{
			return bytes.empty() ? nullptr : bytes.data();
		}

		template <typename value_type>
		auto bytes_for(std::vector<value_type> const& values) noexcept -> VkDeviceSize
		{
			return static_cast<VkDeviceSize>(values.size() * sizeof(value_type));
		}

		auto make_asset_path(char const* relative) -> std::filesystem::path
		{
			return std::filesystem::path{BVN_ASSET_ROOT} / relative;
		}
	}

	vulkan_renderer::vulkan_renderer(rhi::vulkan_context& vulkan_context)
		: context(&vulkan_context)
		, watched_stand(make_asset_path("source/stand.gif"))
	{
		auto pool_info = VkCommandPoolCreateInfo{};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pool_info.queueFamilyIndex = context->graphics_queue_family;
		check(vkCreateCommandPool(context->device, &pool_info, nullptr, &upload_command_pool), "failed to create renderer upload command pool");

		create_grid_resources();
		create_depth_resources();
		create_pipelines();
		upload_sprite_clip(watched_stand.data());
		renderer_swapchain_revision = context->swapchain_revision();
	}

	vulkan_renderer::~vulkan_renderer() noexcept
	{
		if (context != nullptr && context->device != VK_NULL_HANDLE)
		{
			(void)vkDeviceWaitIdle(context->device);
		}

		destroy_sprite_resources();
		destroy_depth_resources();
		destroy_buffer(grid_vertex_buffer);
		destroy_pipelines();

		if (upload_command_pool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(context->device, upload_command_pool, nullptr);
			upload_command_pool = VK_NULL_HANDLE;
		}
	}

	void vulkan_renderer::after_swapchain_recreated()
	{
		destroy_depth_resources();
		create_depth_resources();
		destroy_pipelines();
		create_pipelines();
		renderer_swapchain_revision = context->swapchain_revision();
	}

	void vulkan_renderer::draw(render_scene const& scene, imgui_layer& overlay)
	{
		reload_sprite_texture_if_needed();

		if (renderer_swapchain_revision != context->swapchain_revision())
		{
			after_swapchain_recreated();
		}

		auto frame = context->begin_frame();

		if (frame.command_buffer == VK_NULL_HANDLE)
		{
			return;
		}

		record_frame(frame, scene, overlay);
		context->end_frame();
	}

	void vulkan_renderer::grid_renderable::render(frame_renderer const& ctx) const
	{
		ctx.self.record_grid(ctx.frame, ctx.scene);
	}

	void vulkan_renderer::sprite_renderable::render(frame_renderer const& ctx) const
	{
		ctx.self.record_sprites(ctx.frame, ctx.scene);
	}

	void vulkan_renderer::overlay_renderable::render(frame_renderer const& ctx) const
	{
		ctx.overlay.record(ctx.frame.command_buffer);
	}

	auto vulkan_renderer::record_frame(rhi::frame_context const& frame, render_scene const& scene, imgui_layer& overlay) -> rhi::recording_task
	{
		auto command_buffer = frame.command_buffer;
		context->transition_image(command_buffer, frame.swapchain_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		context->transition_image(command_buffer, depth_image.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

		auto clear_color = VkClearValue{};
		clear_color.color = {{0.03f, 0.035f, 0.045f, 1.0f}};

		auto clear_depth = VkClearValue{};
		clear_depth.depthStencil = {1.0f, 0};

		auto color_attachment = VkRenderingAttachmentInfo{};
		color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachment.imageView = frame.swapchain_image_view;
		color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.clearValue = clear_color;

		auto depth_attachment = VkRenderingAttachmentInfo{};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = depth_image.view;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.clearValue = clear_depth;

		auto render_area = VkRect2D{};
		render_area.offset = {0, 0};
		render_area.extent = frame.extent;

		auto rendering_info = VkRenderingInfo{};
		rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering_info.renderArea = render_area;
		rendering_info.layerCount = 1;
		rendering_info.colorAttachmentCount = 1;
		rendering_info.pColorAttachments = &color_attachment;
		rendering_info.pDepthAttachment = &depth_attachment;

		vkCmdBeginRendering(command_buffer, &rendering_info);
		context->set_full_viewport_and_scissor(command_buffer);

		auto ctx = frame_renderer{*this, frame, scene, overlay};
		display_architecture::render_all(ctx, grid_renderable{}, sprite_renderable{}, overlay_renderable{});

		vkCmdEndRendering(command_buffer);

		context->transition_image(command_buffer, frame.swapchain_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		co_return;
	}

	void vulkan_renderer::create_pipelines()
	{
		auto grid_vertex_shader_words = read_spirv(BVN_GRID_VERT_SPV);
		auto grid_fragment_shader_words = read_spirv(BVN_GRID_FRAG_SPV);
		auto sprite_vertex_shader_words = read_spirv(BVN_SPRITE_VERT_SPV);
		auto sprite_fragment_shader_words = read_spirv(BVN_SPRITE_FRAG_SPV);

		auto grid_vertex_shader = make_shader_module(context->device, grid_vertex_shader_words);
		auto grid_fragment_shader = make_shader_module(context->device, grid_fragment_shader_words);
		auto sprite_vertex_shader = make_shader_module(context->device, sprite_vertex_shader_words);
		auto sprite_fragment_shader = make_shader_module(context->device, sprite_fragment_shader_words);

		auto cleanup_shaders = [&]()
		{
			vkDestroyShaderModule(context->device, sprite_fragment_shader, nullptr);
			vkDestroyShaderModule(context->device, sprite_vertex_shader, nullptr);
			vkDestroyShaderModule(context->device, grid_fragment_shader, nullptr);
			vkDestroyShaderModule(context->device, grid_vertex_shader, nullptr);
		};

		try
		{
			auto grid_push_range = VkPushConstantRange{};
			grid_push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			grid_push_range.offset = 0;
			grid_push_range.size = sizeof(grid_push_constants);

			auto grid_layout_info = VkPipelineLayoutCreateInfo{};
			grid_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			grid_layout_info.pushConstantRangeCount = 1;
			grid_layout_info.pPushConstantRanges = &grid_push_range;
			check(vkCreatePipelineLayout(context->device, &grid_layout_info, nullptr, &grid_pipeline_layout), "failed to create grid pipeline layout");

			auto descriptor_binding = VkDescriptorSetLayoutBinding{};
			descriptor_binding.binding = 0;
			descriptor_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptor_binding.descriptorCount = 1;
			descriptor_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			auto descriptor_layout_info = VkDescriptorSetLayoutCreateInfo{};
			descriptor_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptor_layout_info.bindingCount = 1;
			descriptor_layout_info.pBindings = &descriptor_binding;
			check(vkCreateDescriptorSetLayout(context->device, &descriptor_layout_info, nullptr, &sprite_descriptor_set_layout), "failed to create sprite descriptor set layout");

			auto sprite_push_range = VkPushConstantRange{};
			sprite_push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			sprite_push_range.offset = 0;
			sprite_push_range.size = sizeof(sprite_push_constants);

			auto sprite_layout_info = VkPipelineLayoutCreateInfo{};
			sprite_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			sprite_layout_info.setLayoutCount = 1;
			sprite_layout_info.pSetLayouts = &sprite_descriptor_set_layout;
			sprite_layout_info.pushConstantRangeCount = 1;
			sprite_layout_info.pPushConstantRanges = &sprite_push_range;
			check(vkCreatePipelineLayout(context->device, &sprite_layout_info, nullptr, &sprite_pipeline_layout), "failed to create sprite pipeline layout");

			auto color_format = context->swapchain_image_format;
			auto rendering_info = VkPipelineRenderingCreateInfo{};
			rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
			rendering_info.colorAttachmentCount = 1;
			rendering_info.pColorAttachmentFormats = &color_format;
			rendering_info.depthAttachmentFormat = depth_format;

			auto dynamic_states = std::array{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			auto dynamic_state = VkPipelineDynamicStateCreateInfo{};
			dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
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

			auto depth_stencil = VkPipelineDepthStencilStateCreateInfo{};
			depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depth_stencil.depthTestEnable = VK_TRUE;
			depth_stencil.depthWriteEnable = VK_TRUE;
			depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

			auto grid_binding = VkVertexInputBindingDescription{};
			grid_binding.binding = 0;
			grid_binding.stride = sizeof(grid_vertex);
			grid_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			auto grid_attributes = std::array<VkVertexInputAttributeDescription, 2>
			{
				VkVertexInputAttributeDescription{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(grid_vertex, position)},
				VkVertexInputAttributeDescription{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(grid_vertex, color)},
			};

			auto grid_vertex_input = VkPipelineVertexInputStateCreateInfo{};
			grid_vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			grid_vertex_input.vertexBindingDescriptionCount = 1;
			grid_vertex_input.pVertexBindingDescriptions = &grid_binding;
			grid_vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(grid_attributes.size());
			grid_vertex_input.pVertexAttributeDescriptions = grid_attributes.data();

			auto grid_input_assembly = VkPipelineInputAssemblyStateCreateInfo{};
			grid_input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			grid_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

			auto opaque_blend_attachment = VkPipelineColorBlendAttachmentState{};
			opaque_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			auto color_blend = VkPipelineColorBlendStateCreateInfo{};
			color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			color_blend.attachmentCount = 1;
			color_blend.pAttachments = &opaque_blend_attachment;

			auto grid_vertex_stage = VkPipelineShaderStageCreateInfo{};
			grid_vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			grid_vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
			grid_vertex_stage.module = grid_vertex_shader;
			grid_vertex_stage.pName = "main";

			auto grid_fragment_stage = VkPipelineShaderStageCreateInfo{};
			grid_fragment_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			grid_fragment_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			grid_fragment_stage.module = grid_fragment_shader;
			grid_fragment_stage.pName = "main";

			auto grid_stages = std::array{grid_vertex_stage, grid_fragment_stage};

			auto grid_pipeline_info = VkGraphicsPipelineCreateInfo{};
			grid_pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			grid_pipeline_info.pNext = &rendering_info;
			grid_pipeline_info.stageCount = static_cast<std::uint32_t>(grid_stages.size());
			grid_pipeline_info.pStages = grid_stages.data();
			grid_pipeline_info.pVertexInputState = &grid_vertex_input;
			grid_pipeline_info.pInputAssemblyState = &grid_input_assembly;
			grid_pipeline_info.pViewportState = &viewport_state;
			grid_pipeline_info.pRasterizationState = &rasterization;
			grid_pipeline_info.pMultisampleState = &multisample;
			grid_pipeline_info.pDepthStencilState = &depth_stencil;
			grid_pipeline_info.pColorBlendState = &color_blend;
			grid_pipeline_info.pDynamicState = &dynamic_state;
			grid_pipeline_info.layout = grid_pipeline_layout;
			check(vkCreateGraphicsPipelines(context->device, VK_NULL_HANDLE, 1, &grid_pipeline_info, nullptr, &grid_pipeline), "failed to create grid pipeline");

			auto sprite_binding = VkVertexInputBindingDescription{};
			sprite_binding.binding = 0;
			sprite_binding.stride = sizeof(sprite_vertex);
			sprite_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			auto sprite_attributes = std::array<VkVertexInputAttributeDescription, 2>
			{
				VkVertexInputAttributeDescription{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(sprite_vertex, position)},
				VkVertexInputAttributeDescription{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(sprite_vertex, uv)},
			};

			auto sprite_vertex_input = VkPipelineVertexInputStateCreateInfo{};
			sprite_vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			sprite_vertex_input.vertexBindingDescriptionCount = 1;
			sprite_vertex_input.pVertexBindingDescriptions = &sprite_binding;
			sprite_vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(sprite_attributes.size());
			sprite_vertex_input.pVertexAttributeDescriptions = sprite_attributes.data();

			auto sprite_input_assembly = VkPipelineInputAssemblyStateCreateInfo{};
			sprite_input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			sprite_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			auto sprite_blend_attachment = VkPipelineColorBlendAttachmentState{};
			sprite_blend_attachment.blendEnable = VK_TRUE;
			sprite_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			sprite_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			sprite_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
			sprite_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			sprite_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			sprite_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
			sprite_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			color_blend.pAttachments = &sprite_blend_attachment;

			depth_stencil.depthWriteEnable = VK_FALSE;

			auto sprite_vertex_stage = VkPipelineShaderStageCreateInfo{};
			sprite_vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			sprite_vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
			sprite_vertex_stage.module = sprite_vertex_shader;
			sprite_vertex_stage.pName = "main";

			auto sprite_fragment_stage = VkPipelineShaderStageCreateInfo{};
			sprite_fragment_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			sprite_fragment_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			sprite_fragment_stage.module = sprite_fragment_shader;
			sprite_fragment_stage.pName = "main";

			auto sprite_stages = std::array{sprite_vertex_stage, sprite_fragment_stage};

			auto sprite_pipeline_info = VkGraphicsPipelineCreateInfo{};
			sprite_pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			sprite_pipeline_info.pNext = &rendering_info;
			sprite_pipeline_info.stageCount = static_cast<std::uint32_t>(sprite_stages.size());
			sprite_pipeline_info.pStages = sprite_stages.data();
			sprite_pipeline_info.pVertexInputState = &sprite_vertex_input;
			sprite_pipeline_info.pInputAssemblyState = &sprite_input_assembly;
			sprite_pipeline_info.pViewportState = &viewport_state;
			sprite_pipeline_info.pRasterizationState = &rasterization;
			sprite_pipeline_info.pMultisampleState = &multisample;
			sprite_pipeline_info.pDepthStencilState = &depth_stencil;
			sprite_pipeline_info.pColorBlendState = &color_blend;
			sprite_pipeline_info.pDynamicState = &dynamic_state;
			sprite_pipeline_info.layout = sprite_pipeline_layout;
			check(vkCreateGraphicsPipelines(context->device, VK_NULL_HANDLE, 1, &sprite_pipeline_info, nullptr, &sprite_pipeline), "failed to create sprite pipeline");
		}
		catch (...)
		{
			cleanup_shaders();
			throw;
		}

		cleanup_shaders();
	}

	void vulkan_renderer::destroy_pipelines() noexcept
	{
		if (sprite_pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(context->device, sprite_pipeline, nullptr);
			sprite_pipeline = VK_NULL_HANDLE;
		}

		if (sprite_pipeline_layout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(context->device, sprite_pipeline_layout, nullptr);
			sprite_pipeline_layout = VK_NULL_HANDLE;
		}

		if (sprite_descriptor_set_layout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(context->device, sprite_descriptor_set_layout, nullptr);
			sprite_descriptor_set_layout = VK_NULL_HANDLE;
		}

		if (grid_pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(context->device, grid_pipeline, nullptr);
			grid_pipeline = VK_NULL_HANDLE;
		}

		if (grid_pipeline_layout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(context->device, grid_pipeline_layout, nullptr);
			grid_pipeline_layout = VK_NULL_HANDLE;
		}
	}

	void vulkan_renderer::create_depth_resources()
	{
		create_image(context->swapchain_extent, depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depth_image, VK_IMAGE_ASPECT_DEPTH_BIT);
	}

	void vulkan_renderer::destroy_depth_resources() noexcept
	{
		destroy_image(depth_image);
	}

	void vulkan_renderer::create_grid_resources()
	{
		auto vertices = std::vector<grid_vertex>{};
		vertices.reserve(84);

		for (auto index = -10; index <= 10; ++index)
		{
			auto coordinate = static_cast<float>(index);
			auto major = index == 0;
			auto color_x = major ? glm::vec3{0.85f, 0.18f, 0.16f} : glm::vec3{0.25f, 0.28f, 0.32f};
			auto color_z = major ? glm::vec3{0.20f, 0.62f, 0.24f} : glm::vec3{0.25f, 0.28f, 0.32f};

			vertices.push_back({.position = {-10.0f, 0.0f, coordinate}, .color = color_x});
			vertices.push_back({.position = {10.0f, 0.0f, coordinate}, .color = color_x});
			vertices.push_back({.position = {coordinate, 0.0f, -10.0f}, .color = color_z});
			vertices.push_back({.position = {coordinate, 0.0f, 10.0f}, .color = color_z});
		}

		grid_vertex_count = static_cast<std::uint32_t>(vertices.size());
		create_buffer(bytes_for(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, grid_vertex_buffer);

		auto mapped = static_cast<void*>(nullptr);
		check(vkMapMemory(context->device, grid_vertex_buffer.memory, 0, grid_vertex_buffer.size, 0, &mapped), "failed to map grid vertex buffer");
		std::memcpy(mapped, vertices.data(), static_cast<std::size_t>(grid_vertex_buffer.size));
		vkUnmapMemory(context->device, grid_vertex_buffer.memory);
	}

	void vulkan_renderer::destroy_buffer(vulkan_buffer& buffer) noexcept
	{
		if (buffer.buffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(context->device, buffer.buffer, nullptr);
			buffer.buffer = VK_NULL_HANDLE;
		}

		if (buffer.memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(context->device, buffer.memory, nullptr);
			buffer.memory = VK_NULL_HANDLE;
		}

		buffer.size = 0;
	}

	void vulkan_renderer::destroy_image(vulkan_image& image) noexcept
	{
		if (image.view != VK_NULL_HANDLE)
		{
			vkDestroyImageView(context->device, image.view, nullptr);
			image.view = VK_NULL_HANDLE;
		}

		if (image.image != VK_NULL_HANDLE)
		{
			vkDestroyImage(context->device, image.image, nullptr);
			image.image = VK_NULL_HANDLE;
		}

		if (image.memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(context->device, image.memory, nullptr);
			image.memory = VK_NULL_HANDLE;
		}

		image.format = VK_FORMAT_UNDEFINED;
		image.extent = {};
	}

	void vulkan_renderer::destroy_sprite_resources() noexcept
	{
		destroy_buffer(sprite_vertex_buffer);
		sprite_vertex_capacity = 0;
		destroy_image(sprite_image);

		if (sprite_sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(context->device, sprite_sampler, nullptr);
			sprite_sampler = VK_NULL_HANDLE;
		}

		if (sprite_descriptor_pool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(context->device, sprite_descriptor_pool, nullptr);
			sprite_descriptor_pool = VK_NULL_HANDLE;
			sprite_descriptor_set = VK_NULL_HANDLE;
		}

		sprite_frame_width = 0;
		sprite_frame_height = 0;
		sprite_frame_count = 0;
		sprite_frame_delays.clear();
		uploaded_sprite_revision = 0;
	}

	void vulkan_renderer::reload_sprite_texture_if_needed()
	{
		auto changed = watched_stand.poll();

		if (!changed && watched_stand.revision() == uploaded_sprite_revision)
		{
			return;
		}

		context->wait_idle();
		destroy_sprite_resources();
		upload_sprite_clip(watched_stand.data());
	}

	void vulkan_renderer::upload_sprite_clip(assets::sprite_clip_data const& clip)
	{
		auto atlas = assets::pack_horizontal_atlas(clip);
		sprite_frame_width = clip.frame_width;
		sprite_frame_height = clip.frame_height;
		sprite_frame_count = static_cast<std::uint32_t>(clip.frames.size());
		sprite_frame_delays.clear();
		sprite_frame_delays.reserve(clip.frames.size());

		for (auto const& frame : clip.frames)
		{
			sprite_frame_delays.push_back(frame.delay);
		}

		create_image({.width = atlas.width, .height = atlas.height}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sprite_image, VK_IMAGE_ASPECT_COLOR_BIT);

		auto staging = vulkan_buffer{};
		create_buffer(static_cast<VkDeviceSize>(atlas.pixels.size()), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging);

		auto mapped = static_cast<void*>(nullptr);
		check(vkMapMemory(context->device, staging.memory, 0, staging.size, 0, &mapped), "failed to map sprite staging buffer");
		std::memcpy(mapped, device_address(atlas.pixels), atlas.pixels.size());
		vkUnmapMemory(context->device, staging.memory);

		auto command_buffer = begin_upload_commands();
		context->transition_image(command_buffer, sprite_image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		auto copy_region = VkBufferImageCopy{};
		copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.imageSubresource.mipLevel = 0;
		copy_region.imageSubresource.baseArrayLayer = 0;
		copy_region.imageSubresource.layerCount = 1;
		copy_region.imageExtent = {atlas.width, atlas.height, 1};
		vkCmdCopyBufferToImage(command_buffer, staging.buffer, sprite_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

		context->transition_image(command_buffer, sprite_image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		end_upload_commands(command_buffer);
		destroy_buffer(staging);

		auto sampler_info = VkSamplerCreateInfo{};
		sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler_info.magFilter = VK_FILTER_NEAREST;
		sampler_info.minFilter = VK_FILTER_NEAREST;
		sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.maxLod = 1.0f;
		check(vkCreateSampler(context->device, &sampler_info, nullptr, &sprite_sampler), "failed to create sprite sampler");

		auto pool_size = VkDescriptorPoolSize{};
		pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		pool_size.descriptorCount = 1;

		auto pool_info = VkDescriptorPoolCreateInfo{};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.maxSets = 1;
		pool_info.poolSizeCount = 1;
		pool_info.pPoolSizes = &pool_size;
		check(vkCreateDescriptorPool(context->device, &pool_info, nullptr, &sprite_descriptor_pool), "failed to create sprite descriptor pool");

		auto allocate_info = VkDescriptorSetAllocateInfo{};
		allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocate_info.descriptorPool = sprite_descriptor_pool;
		allocate_info.descriptorSetCount = 1;
		allocate_info.pSetLayouts = &sprite_descriptor_set_layout;
		check(vkAllocateDescriptorSets(context->device, &allocate_info, &sprite_descriptor_set), "failed to allocate sprite descriptor set");

		auto image_info = VkDescriptorImageInfo{};
		image_info.sampler = sprite_sampler;
		image_info.imageView = sprite_image.view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		auto write = VkWriteDescriptorSet{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = sprite_descriptor_set;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;
		vkUpdateDescriptorSets(context->device, 1, &write, 0, nullptr);

		uploaded_sprite_revision = watched_stand.revision();
	}

	void vulkan_renderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties, vulkan_buffer& buffer)
	{
		auto create_info = VkBufferCreateInfo{};
		create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		create_info.size = size;
		create_info.usage = usage;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		check(vkCreateBuffer(context->device, &create_info, nullptr, &buffer.buffer), "failed to create Vulkan buffer");

		auto requirements = VkMemoryRequirements{};
		vkGetBufferMemoryRequirements(context->device, buffer.buffer, &requirements);

		auto allocate_info = VkMemoryAllocateInfo{};
		allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocate_info.allocationSize = requirements.size;
		allocate_info.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, memory_properties);
		check(vkAllocateMemory(context->device, &allocate_info, nullptr, &buffer.memory), "failed to allocate Vulkan buffer memory");
		check(vkBindBufferMemory(context->device, buffer.buffer, buffer.memory, 0), "failed to bind Vulkan buffer memory");
		buffer.size = size;
	}

	void vulkan_renderer::create_image(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags memory_properties, vulkan_image& image, VkImageAspectFlags aspect)
	{
		auto image_info = VkImageCreateInfo{};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.extent = {extent.width, extent.height, 1};
		image_info.mipLevels = 1;
		image_info.arrayLayers = 1;
		image_info.format = format;
		image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_info.usage = usage;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		check(vkCreateImage(context->device, &image_info, nullptr, &image.image), "failed to create Vulkan image");

		auto requirements = VkMemoryRequirements{};
		vkGetImageMemoryRequirements(context->device, image.image, &requirements);

		auto allocate_info = VkMemoryAllocateInfo{};
		allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocate_info.allocationSize = requirements.size;
		allocate_info.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, memory_properties);
		check(vkAllocateMemory(context->device, &allocate_info, nullptr, &image.memory), "failed to allocate Vulkan image memory");
		check(vkBindImageMemory(context->device, image.image, image.memory, 0), "failed to bind Vulkan image memory");

		auto view_info = VkImageViewCreateInfo{};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.image = image.image;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format = format;
		view_info.subresourceRange.aspectMask = aspect;
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.layerCount = 1;
		check(vkCreateImageView(context->device, &view_info, nullptr, &image.view), "failed to create Vulkan image view");

		image.format = format;
		image.extent = extent;
	}

	auto vulkan_renderer::find_memory_type(std::uint32_t type_filter, VkMemoryPropertyFlags properties) const -> std::uint32_t
	{
		auto memory_properties = VkPhysicalDeviceMemoryProperties{};
		vkGetPhysicalDeviceMemoryProperties(context->physical_device, &memory_properties);

		for (auto index = std::uint32_t{}; index < memory_properties.memoryTypeCount; ++index)
		{
			if ((type_filter & (1u << index)) != 0 && (memory_properties.memoryTypes[index].propertyFlags & properties) == properties)
			{
				return index;
			}
		}

		throw std::runtime_error{"failed to find matching Vulkan memory type"};
	}

	auto vulkan_renderer::begin_upload_commands() -> VkCommandBuffer
	{
		auto allocate_info = VkCommandBufferAllocateInfo{};
		allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocate_info.commandPool = upload_command_pool;
		allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate_info.commandBufferCount = 1;

		auto command_buffer = VkCommandBuffer{};
		check(vkAllocateCommandBuffers(context->device, &allocate_info, &command_buffer), "failed to allocate upload command buffer");

		auto begin_info = VkCommandBufferBeginInfo{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		check(vkBeginCommandBuffer(command_buffer, &begin_info), "failed to begin upload command buffer");

		return command_buffer;
	}

	void vulkan_renderer::end_upload_commands(VkCommandBuffer command_buffer)
	{
		check(vkEndCommandBuffer(command_buffer), "failed to end upload command buffer");

		auto submit_info = VkSubmitInfo{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffer;

		check(vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE), "failed to submit upload command buffer");
		check(vkQueueWaitIdle(context->graphics_queue), "failed to wait for upload queue");
		vkFreeCommandBuffers(context->device, upload_command_pool, 1, &command_buffer);
	}

	void vulkan_renderer::record_grid(rhi::frame_context const& frame, render_scene const& scene)
	{
		auto command_buffer = frame.command_buffer;
		auto view_projection = scene.view_camera.projection * scene.view_camera.view;
		auto push = grid_push_constants{.view_projection = view_projection};
		auto offset = VkDeviceSize{};

		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, grid_pipeline);
		vkCmdBindVertexBuffers(command_buffer, 0, 1, &grid_vertex_buffer.buffer, &offset);
		vkCmdPushConstants(command_buffer, grid_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
		vkCmdDraw(command_buffer, grid_vertex_count, 1, 0, 0);
	}

	void vulkan_renderer::record_sprites(rhi::frame_context const& frame, render_scene const& scene)
	{
		if (scene.sprites.empty() || sprite_frame_count == 0 || sprite_descriptor_set == VK_NULL_HANDLE)
		{
			return;
		}

		auto vertex_count = static_cast<std::uint32_t>(scene.sprites.size() * 6u);

		if (vertex_count > sprite_vertex_capacity)
		{
			context->wait_idle();
			destroy_buffer(sprite_vertex_buffer);
			sprite_vertex_capacity = std::max(vertex_count, sprite_vertex_capacity * 2u);
			create_buffer(static_cast<VkDeviceSize>(sprite_vertex_capacity * sizeof(sprite_vertex)), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sprite_vertex_buffer);
		}

		auto vertices = std::vector<sprite_vertex>{};
		vertices.reserve(vertex_count);

		// Screen-aligned upright billboard: width follows the camera's horizontal right axis,
		// height stays along world up (+Y) so characters always stand vertically on the floor.
		auto const& view = scene.view_camera.view;
		auto camera_right = glm::vec3{view[0][0], view[1][0], view[2][0]};
		auto billboard_right = glm::vec3{camera_right.x, 0.0f, camera_right.z};
		billboard_right = glm::length(billboard_right) > 1.0e-4f ? glm::normalize(billboard_right) : glm::vec3{1.0f, 0.0f, 0.0f};
		auto billboard_up = glm::vec3{0.0f, 1.0f, 0.0f};

		for (auto const& sprite : scene.sprites)
		{
			auto frame_index = sprite_frame_index(sprite.animation_tick);
			auto u0 = static_cast<float>(frame_index) / static_cast<float>(sprite_frame_count);
			auto u1 = static_cast<float>(frame_index + 1) / static_cast<float>(sprite_frame_count);

			if (!sprite.facing_right)
			{
				std::swap(u0, u1);
			}

			auto width = static_cast<float>(sprite_frame_width) * sprite_world_scale;
			auto height = static_cast<float>(sprite_frame_height) * sprite_world_scale;
			auto half_step = billboard_right * (width * 0.5f);
			auto rise = billboard_up * height;
			auto feet = sprite.position;

			auto bottom_left = feet - half_step;
			auto bottom_right = feet + half_step;
			auto top_left = bottom_left + rise;
			auto top_right = bottom_right + rise;

			vertices.push_back({.position = top_left, .uv = {u0, 0.0f}});
			vertices.push_back({.position = bottom_left, .uv = {u0, 1.0f}});
			vertices.push_back({.position = bottom_right, .uv = {u1, 1.0f}});
			vertices.push_back({.position = top_left, .uv = {u0, 0.0f}});
			vertices.push_back({.position = bottom_right, .uv = {u1, 1.0f}});
			vertices.push_back({.position = top_right, .uv = {u1, 0.0f}});
		}

		auto mapped = static_cast<void*>(nullptr);
		check(vkMapMemory(context->device, sprite_vertex_buffer.memory, 0, static_cast<VkDeviceSize>(vertices.size() * sizeof(sprite_vertex)), 0, &mapped), "failed to map sprite vertex buffer");
		std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(sprite_vertex));
		vkUnmapMemory(context->device, sprite_vertex_buffer.memory);

		auto command_buffer = frame.command_buffer;
		auto offset = VkDeviceSize{};
		auto view_projection = scene.view_camera.projection * scene.view_camera.view;
		auto push = sprite_push_constants{.view_projection = view_projection};

		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline);
		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline_layout, 0, 1, &sprite_descriptor_set, 0, nullptr);
		vkCmdBindVertexBuffers(command_buffer, 0, 1, &sprite_vertex_buffer.buffer, &offset);
		vkCmdPushConstants(command_buffer, sprite_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
		vkCmdDraw(command_buffer, static_cast<std::uint32_t>(vertices.size()), 1, 0, 0);
	}

	auto vulkan_renderer::sprite_frame_index(std::uint64_t animation_tick) const noexcept -> std::uint32_t
	{
		if (sprite_frame_delays.empty())
		{
			return 0;
		}

		auto duration = std::chrono::milliseconds{};

		for (auto delay : sprite_frame_delays)
		{
			duration += delay;
		}

		if (duration.count() <= 0)
		{
			return static_cast<std::uint32_t>(animation_tick % sprite_frame_delays.size());
		}

		auto elapsed = std::chrono::milliseconds{static_cast<long long>(animation_tick * 1000u / 30u)};
		auto local_time = std::chrono::milliseconds{elapsed.count() % duration.count()};
		auto accumulated = std::chrono::milliseconds{};

		for (auto index = std::size_t{}; index < sprite_frame_delays.size(); ++index)
		{
			accumulated += sprite_frame_delays[index];

			if (local_time < accumulated)
			{
				return static_cast<std::uint32_t>(index);
			}
		}

		return static_cast<std::uint32_t>(sprite_frame_delays.size() - 1u);
	}
}
