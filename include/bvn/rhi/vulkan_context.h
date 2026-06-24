#pragma once

#include <coroutine>
#include <cstdint>
#include <functional>
#include <vector>

#include <VkBootstrap.h>
#include <vulkan/vulkan.h>

namespace bvn::rhi
{
	struct surface_extent
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	struct vulkan_context_create_info
	{
		std::vector<char const*> instance_extensions;
		std::function<VkSurfaceKHR(VkInstance)> create_surface;
		surface_extent initial_extent;
		bool enable_validation = true;
	};

	struct frame_context
	{
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		VkImage swapchain_image = VK_NULL_HANDLE;
		VkImageView swapchain_image_view = VK_NULL_HANDLE;
		std::uint32_t image_index = 0;
		VkFormat color_format = VK_FORMAT_UNDEFINED;
		VkExtent2D extent = {};
	};

	struct recording_task
	{
		struct promise_type
		{
			auto get_return_object() noexcept -> recording_task;
			auto initial_suspend() const noexcept -> std::suspend_never;
			auto final_suspend() const noexcept -> std::suspend_never;
			void return_void() const noexcept;
			void unhandled_exception() const;
		};
	};

	struct vulkan_context
	{
	public:
		explicit vulkan_context(vulkan_context_create_info create_info);
		~vulkan_context();

		vulkan_context(vulkan_context const&) = delete;
		auto operator=(vulkan_context const&) -> vulkan_context& = delete;

		vulkan_context(vulkan_context&&) = delete;
		auto operator=(vulkan_context&&) -> vulkan_context& = delete;

		VkInstance instance = VK_NULL_HANDLE;
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		VkQueue graphics_queue = VK_NULL_HANDLE;
		std::uint32_t graphics_queue_family = 0;
		VkQueue present_queue = VK_NULL_HANDLE;
		std::uint32_t present_queue_family = 0;
		char device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {};

		VkFormat swapchain_image_format = VK_FORMAT_UNDEFINED;
		VkExtent2D swapchain_extent = {};
		std::vector<VkImage> swapchain_images;

		auto swapchain_revision() const noexcept -> std::uint64_t;
		void resize(surface_extent new_extent);
		auto begin_frame() -> frame_context;
		void end_frame();
		void wait_idle() const;
		void transition_image(VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags aspect, VkImageLayout old_layout, VkImageLayout new_layout) const noexcept;
		void set_full_viewport_and_scissor(VkCommandBuffer command_buffer) const noexcept;

	private:
		struct frame_resources
		{
			VkCommandPool command_pool = VK_NULL_HANDLE;
			VkCommandBuffer command_buffer = VK_NULL_HANDLE;
			VkSemaphore image_available = VK_NULL_HANDLE;
			VkSemaphore render_finished = VK_NULL_HANDLE;
			VkFence in_flight = VK_NULL_HANDLE;
		};

		void create_instance(std::vector<char const*> const& extensions, bool enable_validation);
		void create_device();
		void create_swapchain(surface_extent requested_extent);
		void create_frame_resources();
		void destroy_frame_resources() noexcept;
		void destroy_swapchain() noexcept;
		void destroy_device() noexcept;
		void destroy_instance() noexcept;

		VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		std::vector<VkImageView> swapchain_image_views;

		std::vector<frame_resources> frames;
		std::uint32_t current_frame = 0;
		std::uint32_t active_image_index = 0;
		std::uint64_t current_swapchain_revision = 0;
		bool frame_active = false;
		vkb::Instance bootstrap_instance;
		vkb::Device bootstrap_device;
	};
}
