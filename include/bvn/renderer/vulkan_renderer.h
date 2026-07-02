#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include <VkBootstrap.h>
#include <vulkan/vulkan.h>

#include <bvn/platform/window.h>
#include <vkkl/vkkl.h>

namespace bvn::renderer
{
	inline constexpr auto frames_in_flight = 2u;

	struct vulkan_image
	{
		::vkkl::device_memory memory;
		::vkkl::image image;
		::vkkl::image_view view;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkExtent2D extent = {};
	};

	// Renderer (renderer.md §2/§3). Pure data context owning framework-level Vulkan objects.
	// Per-frame begin/render/submit/present lives in render_workflow::submit().
	struct vulkan_renderer
	{
		explicit vulkan_renderer(::bvn::platform::window const& window, bool enable_validation = true);
		~vulkan_renderer() noexcept;

		vulkan_renderer(vulkan_renderer const&) = delete;
		auto operator=(vulkan_renderer const&) -> vulkan_renderer& = delete;

		vulkan_renderer(vulkan_renderer&&) = delete;
		auto operator=(vulkan_renderer&&) -> vulkan_renderer& = delete;

		void resize(::bvn::platform::window_extent new_extent);

		::bvn::platform::window const* target_window = nullptr;
		::vkkl::instance instance;
		::vkkl::debug_utils_messenger debug_messenger;
		::vkkl::surface surface;
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		::vkkl::device device;
		VkQueue graphics_queue = VK_NULL_HANDLE;
		::std::uint32_t graphics_queue_family = 0;
		VkQueue present_queue = VK_NULL_HANDLE;
		::std::uint32_t present_queue_family = 0;
		char device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {};

		::vkkl::swapchain swapchain;
		VkFormat swapchain_image_format = VK_FORMAT_UNDEFINED;
		VkExtent2D swapchain_extent = {};
		::std::vector<VkImage> swapchain_images;
		::std::vector<::vkkl::image_view> swapchain_image_views;
		::std::vector<::vkkl::semaphore> render_finished;
		::std::uint64_t swapchain_revision = 0;

		static constexpr auto depth_format = VK_FORMAT_D32_SFLOAT;

		struct frame_slot
		{
			::vkkl::command_pool command_pool;
			VkCommandBuffer command_buffer = VK_NULL_HANDLE;
			::vkkl::semaphore image_available;
			::vkkl::fence in_flight;
			vulkan_image depth_image;
		};

		::vkb::Instance bootstrap_instance;
		::vkb::Device bootstrap_device;

		::std::array<frame_slot, frames_in_flight> slots = {};
		::std::uint32_t current_slot = 0;
		::std::uint32_t active_image_index = 0;
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		::std::atomic_bool resize_requested{false};
		::bvn::platform::window_extent requested_extent{};
	};
}
