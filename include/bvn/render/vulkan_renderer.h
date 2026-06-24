#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <bvn/assets/sprite_clip.h>
#include <bvn/render/imgui_layer.h>
#include <bvn/render/render_scene.h>
#include <bvn/rhi/vulkan_context.h>

namespace bvn::render
{
	struct vulkan_renderer
	{
	public:
		explicit vulkan_renderer(rhi::vulkan_context& context);
		~vulkan_renderer() noexcept;

		vulkan_renderer(vulkan_renderer const&) = delete;
		auto operator=(vulkan_renderer const&) -> vulkan_renderer& = delete;

		vulkan_renderer(vulkan_renderer&&) = delete;
		auto operator=(vulkan_renderer&&) -> vulkan_renderer& = delete;

		void after_swapchain_recreated();
		void draw(render_scene const& scene, imgui_layer& overlay);

	private:
		// Per-frame drawing context handed to renderables; the renderer doubles as the ctx
		// while a dedicated rhi-facing ctx layer stays deferred (display-architecture §6).
		struct frame_renderer
		{
		public:
			vulkan_renderer& self;
			rhi::frame_context const& frame;
			render_scene const& scene;
			imgui_layer& overlay;
		};

		// Each visible thing draws itself through display_architecture::render (display-architecture §1).
		struct grid_renderable
		{
		public:
			void render(frame_renderer const& ctx) const;
		};

		struct sprite_renderable
		{
		public:
			void render(frame_renderer const& ctx) const;
		};

		struct overlay_renderable
		{
		public:
			void render(frame_renderer const& ctx) const;
		};

		struct vulkan_buffer
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkDeviceSize size = 0;
		};

		struct vulkan_image
		{
			VkImage image = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			VkFormat format = VK_FORMAT_UNDEFINED;
			VkExtent2D extent = {};
		};

		struct grid_vertex
		{
			glm::vec3 position = {};
			glm::vec3 color = {};
		};

		struct sprite_vertex
		{
			glm::vec3 position = {};
			glm::vec2 uv = {};
		};

		auto record_frame(rhi::frame_context const& frame, render_scene const& scene, imgui_layer& overlay) -> rhi::recording_task;
		void create_pipelines();
		void destroy_pipelines() noexcept;
		void create_depth_resources();
		void destroy_depth_resources() noexcept;
		void create_grid_resources();
		void destroy_buffer(vulkan_buffer& buffer) noexcept;
		void destroy_image(vulkan_image& image) noexcept;
		void destroy_sprite_resources() noexcept;
		void reload_sprite_texture_if_needed();
		void upload_sprite_clip(assets::sprite_clip_data const& clip);
		void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties, vulkan_buffer& buffer);
		void create_image(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags memory_properties, vulkan_image& image, VkImageAspectFlags aspect);
		auto find_memory_type(std::uint32_t type_filter, VkMemoryPropertyFlags properties) const -> std::uint32_t;
		auto begin_upload_commands() -> VkCommandBuffer;
		void end_upload_commands(VkCommandBuffer command_buffer);
		void record_grid(rhi::frame_context const& frame, render_scene const& scene);
		void record_sprites(rhi::frame_context const& frame, render_scene const& scene);
		auto sprite_frame_index(std::uint64_t animation_tick) const noexcept -> std::uint32_t;

		rhi::vulkan_context* context = nullptr;
		VkCommandPool upload_command_pool = VK_NULL_HANDLE;
		VkPipelineLayout grid_pipeline_layout = VK_NULL_HANDLE;
		VkPipeline grid_pipeline = VK_NULL_HANDLE;
		VkPipelineLayout sprite_pipeline_layout = VK_NULL_HANDLE;
		VkPipeline sprite_pipeline = VK_NULL_HANDLE;
		VkDescriptorSetLayout sprite_descriptor_set_layout = VK_NULL_HANDLE;
		VkDescriptorPool sprite_descriptor_pool = VK_NULL_HANDLE;
		VkDescriptorSet sprite_descriptor_set = VK_NULL_HANDLE;
		VkSampler sprite_sampler = VK_NULL_HANDLE;
		vulkan_buffer grid_vertex_buffer;
		std::uint32_t grid_vertex_count = 0;
		vulkan_buffer sprite_vertex_buffer;
		std::uint32_t sprite_vertex_capacity = 0;
		vulkan_image depth_image;
		vulkan_image sprite_image;
		std::uint32_t sprite_frame_width = 0;
		std::uint32_t sprite_frame_height = 0;
		std::uint32_t sprite_frame_count = 0;
		std::vector<std::chrono::milliseconds> sprite_frame_delays;
		std::uint64_t uploaded_sprite_revision = 0;
		std::uint64_t renderer_swapchain_revision = 0;
		assets::watched_sprite_clip watched_stand;
	};
}
