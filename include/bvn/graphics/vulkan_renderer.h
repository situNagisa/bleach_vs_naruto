#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <VkBootstrap.h>
#include <vulkan/vulkan.h>

#include <bvn/graphics/renderer.h>
#include <bvn/graphics/environment.h>
#include <bvn/platform/window.h>
#include <vkkl/vkkl.h>

NAGISA_BUILD_LIB_DETAIL_BEGIN

inline constexpr auto frames_in_flight = 2u;

struct vulkan_image
{
	::vkkl::device_memory _memory;
	::vkkl::image _image;
	::vkkl::image_view _view;
	VkFormat _format = VK_FORMAT_UNDEFINED;
	VkExtent2D _extent = {};
};

struct vulkan_context;

struct global_vulkan_env_renderer
{
	vulkan_context const* _context = nullptr;

	auto instance() const noexcept -> VkInstance;
	auto physical_device() const noexcept -> VkPhysicalDevice;
	auto device() const noexcept -> VkDevice;
	auto graphics_queue() const noexcept -> VkQueue;
	auto graphics_queue_family() const noexcept -> ::std::uint32_t;
	auto swapchain() const noexcept -> VkSwapchainKHR;
	auto swapchain_extent() const noexcept -> VkExtent2D;
	auto swapchain_image_format() const noexcept -> VkFormat;
	auto swapchain_image_count() const noexcept -> ::std::uint32_t;
	auto swapchain_images() const -> ::std::vector<VkImage>;
	auto swapchain_image_views() const -> ::std::vector<VkImageView>;
	auto swapchain_render_finished() const -> ::std::vector<VkSemaphore>;
	auto depth_format() const noexcept -> VkFormat;
	auto device_name() const noexcept -> char const*;
};

struct frame_vulkan_env_renderer
{
	vulkan_context const* _context = nullptr;
	::std::uint32_t _slot_index = 0;
	::std::uint32_t _active_image_index = 0;

	auto in_flight() const noexcept -> VkFence;
	auto image_available() const noexcept -> VkSemaphore;
	auto active_image_index() const noexcept -> ::std::uint32_t;
	auto depth_image() const noexcept -> VkImage;
	auto depth_image_view() const noexcept -> VkImageView;
};

struct vulkan_context
{
	struct frame_slot
	{
		::vkkl::command_pool primary_command_pool;
		VkCommandBuffer primary_command_buffer = VK_NULL_HANDLE;
		::vkkl::semaphore image_available;
		::vkkl::fence in_flight;
		vulkan_image depth_image;
	};

	~vulkan_context() noexcept
	{
		if (_device.handle != VK_NULL_HANDLE)
		{
			(void)::vkDeviceWaitIdle(_device.handle);
		}

		for (auto&& slot : _slots)
		{
			slot.depth_image._view = {};
			slot.depth_image._image = {};
			slot.depth_image._memory = {};
			slot.depth_image._format = VK_FORMAT_UNDEFINED;
			slot.depth_image._extent = {};
			slot.in_flight = {};
			slot.image_available = {};
			slot.primary_command_pool = {};
			slot.primary_command_buffer = VK_NULL_HANDLE;
		}

		_render_finished_handles.clear();
		_render_finished.clear();
		_swapchain_image_view_handles.clear();
		_swapchain_image_views.clear();
		_swapchain_images.clear();
		_swapchain = {};

		_device = {};
		_physical_device = VK_NULL_HANDLE;
		_graphics_queue = VK_NULL_HANDLE;
		_present_queue = VK_NULL_HANDLE;
		_surface = {};
		_debug_messenger = {};
		_instance = {};
	}

	vulkan_context() = default;
	vulkan_context(vulkan_context const&) = delete;
	auto operator=(vulkan_context const&) -> vulkan_context& = delete;
	vulkan_context(vulkan_context&&) = delete;
	auto operator=(vulkan_context&&) -> vulkan_context& = delete;

	constexpr auto global_env() const noexcept -> global_vulkan_env_renderer
	{
		return {._context = this};
	}

	::bvn::platform::window const* _target_window = nullptr;
	::vkkl::instance _instance;
	::vkkl::debug_utils_messenger _debug_messenger;
	::vkkl::surface _surface;
	VkPhysicalDevice _physical_device = VK_NULL_HANDLE;
	::vkkl::device _device;
	VkQueue _graphics_queue = VK_NULL_HANDLE;
	::std::uint32_t _graphics_queue_family = 0;
	VkQueue _present_queue = VK_NULL_HANDLE;
	::std::uint32_t _present_queue_family = 0;
	char _device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {};

	::vkkl::swapchain _swapchain;
	VkFormat _swapchain_image_format = VK_FORMAT_UNDEFINED;
	VkExtent2D _swapchain_extent = {};
	::std::vector<VkImage> _swapchain_images;
	::std::vector<::vkkl::image_view> _swapchain_image_views;
	::std::vector<VkImageView> _swapchain_image_view_handles;
	::std::vector<::vkkl::semaphore> _render_finished;
	::std::vector<VkSemaphore> _render_finished_handles;
	::std::uint64_t _swapchain_revision = 0;

	static constexpr auto _depth_format = VK_FORMAT_D32_SFLOAT;

	::vkb::Instance _bootstrap_instance;
	::vkb::Device _bootstrap_device;

	::std::array<frame_slot, frames_in_flight> _slots = {};
	::std::atomic_bool _resize_requested{false};
	::bvn::platform::window_extent _requested_extent{};
};

inline auto global_vulkan_env_renderer::instance() const noexcept -> VkInstance
{
	return _context->_instance.handle;
}

inline auto global_vulkan_env_renderer::physical_device() const noexcept -> VkPhysicalDevice
{
	return _context->_physical_device;
}

inline auto global_vulkan_env_renderer::device() const noexcept -> VkDevice
{
	return _context->_device.handle;
}

inline auto global_vulkan_env_renderer::graphics_queue() const noexcept -> VkQueue
{
	return _context->_graphics_queue;
}

inline auto global_vulkan_env_renderer::graphics_queue_family() const noexcept -> ::std::uint32_t
{
	return _context->_graphics_queue_family;
}

inline auto global_vulkan_env_renderer::swapchain() const noexcept -> VkSwapchainKHR
{
	return _context->_swapchain.handle;
}

inline auto global_vulkan_env_renderer::swapchain_extent() const noexcept -> VkExtent2D
{
	return _context->_swapchain_extent;
}

inline auto global_vulkan_env_renderer::swapchain_image_format() const noexcept -> VkFormat
{
	return _context->_swapchain_image_format;
}

inline auto global_vulkan_env_renderer::swapchain_image_count() const noexcept -> ::std::uint32_t
{
	return static_cast<::std::uint32_t>(_context->_swapchain_images.size());
}

inline auto global_vulkan_env_renderer::swapchain_images() const -> ::std::vector<VkImage>
{
	return _context->_swapchain_images;
}

inline auto global_vulkan_env_renderer::swapchain_image_views() const -> ::std::vector<VkImageView>
{
	return _context->_swapchain_image_view_handles;
}

inline auto global_vulkan_env_renderer::swapchain_render_finished() const -> ::std::vector<VkSemaphore>
{
	return _context->_render_finished_handles;
}

inline auto global_vulkan_env_renderer::depth_format() const noexcept -> VkFormat
{
	return vulkan_context::_depth_format;
}

inline auto global_vulkan_env_renderer::device_name() const noexcept -> char const*
{
	return _context->_device_name;
}

inline auto frame_vulkan_env_renderer::in_flight() const noexcept -> VkFence
{
	return _context->_slots[_slot_index].in_flight.handle;
}

inline auto frame_vulkan_env_renderer::image_available() const noexcept -> VkSemaphore
{
	return _context->_slots[_slot_index].image_available.handle;
}

inline auto frame_vulkan_env_renderer::active_image_index() const noexcept -> ::std::uint32_t
{
	return _active_image_index;
}

inline auto frame_vulkan_env_renderer::depth_image() const noexcept -> VkImage
{
	return _context->_slots[_slot_index].depth_image._image.handle;
}

inline auto frame_vulkan_env_renderer::depth_image_view() const noexcept -> VkImageView
{
	return _context->_slots[_slot_index].depth_image._view.handle;
}

inline void resize(vulkan_context& renderer, ::bvn::platform::window_extent new_extent)
{
	if (new_extent.width == 0 || new_extent.height == 0)
	{
		return;
	}

	if (renderer._device.handle != VK_NULL_HANDLE && ::vkDeviceWaitIdle(renderer._device.handle) != ::VK_SUCCESS)
	{
		throw ::std::runtime_error{"failed to wait for Vulkan device idle"};
	}

	for (auto&& slot : renderer._slots)
	{
		slot.depth_image._view = {};
		slot.depth_image._image = {};
		slot.depth_image._memory = {};
		slot.depth_image._format = VK_FORMAT_UNDEFINED;
		slot.depth_image._extent = {};
	}

	renderer._render_finished_handles.clear();
	renderer._render_finished.clear();
	renderer._swapchain_image_view_handles.clear();
	renderer._swapchain_image_views.clear();
	renderer._swapchain_images.clear();
	renderer._swapchain = {};

	auto swapchain_builder = ::vkb::SwapchainBuilder{
		renderer._physical_device,
		renderer._device.handle,
		renderer._surface.handle,
		renderer._graphics_queue_family,
		renderer._present_queue_family,
	};
	swapchain_builder.set_desired_extent(::std::max(new_extent.width, 1u), ::std::max(new_extent.height, 1u));
	swapchain_builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
	swapchain_builder.set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
	swapchain_builder.add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

	auto swapchain_result = swapchain_builder.build();
	if (!swapchain_result)
	{
		throw ::std::runtime_error{"failed to create Vulkan swapchain"};
	}

	auto created_swapchain = swapchain_result.value();
	renderer._swapchain = ::vkkl::swapchain{renderer._device.handle, created_swapchain.swapchain};
	renderer._swapchain_image_format = created_swapchain.image_format;
	renderer._swapchain_extent = created_swapchain.extent;

	auto image_result = created_swapchain.get_images();
	if (!image_result)
	{
		throw ::std::runtime_error{"failed to get swapchain images"};
	}
	renderer._swapchain_images = image_result.value();

	auto next_image_views = ::std::vector<::vkkl::image_view>{};
	auto next_image_view_handles = ::std::vector<VkImageView>{};
	next_image_views.reserve(renderer._swapchain_images.size());
	next_image_view_handles.reserve(renderer._swapchain_images.size());
	for (auto image : renderer._swapchain_images)
	{
		auto image_view_info = VkImageViewCreateInfo{};
		image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_info.image = image;
		image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		image_view_info.format = renderer._swapchain_image_format;
		image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_info.subresourceRange.baseMipLevel = 0;
		image_view_info.subresourceRange.levelCount = 1;
		image_view_info.subresourceRange.baseArrayLayer = 0;
		image_view_info.subresourceRange.layerCount = 1;

		auto image_view = VkImageView{};
		if (::vkCreateImageView(renderer._device.handle, &image_view_info, nullptr, &image_view) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to create swapchain image view"};
		}
		next_image_views.emplace_back(renderer._device.handle, image_view);
		next_image_view_handles.push_back(image_view);
	}
	renderer._swapchain_image_views = ::std::move(next_image_views);
	renderer._swapchain_image_view_handles = ::std::move(next_image_view_handles);

	auto semaphore_info = VkSemaphoreCreateInfo{};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	auto next_render_finished = ::std::vector<::vkkl::semaphore>{};
	auto next_render_finished_handles = ::std::vector<VkSemaphore>{};
	next_render_finished.reserve(renderer._swapchain_images.size());
	next_render_finished_handles.reserve(renderer._swapchain_images.size());

	for ([[maybe_unused]] auto image : renderer._swapchain_images)
	{
		auto semaphore = VkSemaphore{};
		if (::vkCreateSemaphore(renderer._device.handle, &semaphore_info, nullptr, &semaphore) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to create per-image render-finished semaphore"};
		}
		next_render_finished.emplace_back(renderer._device.handle, semaphore);
		next_render_finished_handles.push_back(semaphore);
	}
	renderer._render_finished = ::std::move(next_render_finished);
	renderer._render_finished_handles = ::std::move(next_render_finished_handles);

	for (auto&& slot : renderer._slots)
	{
		auto image_info = VkImageCreateInfo{};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.extent = {renderer._swapchain_extent.width, renderer._swapchain_extent.height, 1};
		image_info.mipLevels = 1;
		image_info.arrayLayers = 1;
		image_info.format = vulkan_context::_depth_format;
		image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		auto depth_image = VkImage{};
		if (::vkCreateImage(renderer._device.handle, &image_info, nullptr, &depth_image) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to create depth image"};
		}
		slot.depth_image._image = ::vkkl::image{renderer._device.handle, depth_image};

		auto requirements = VkMemoryRequirements{};
		::vkGetImageMemoryRequirements(renderer._device.handle, slot.depth_image._image.handle, &requirements);

		auto allocate_info = VkMemoryAllocateInfo{};
		allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocate_info.allocationSize = requirements.size;

		auto memory_properties = VkPhysicalDeviceMemoryProperties{};
		::vkGetPhysicalDeviceMemoryProperties(renderer._physical_device, &memory_properties);
		auto found_memory_type = false;

		for (auto index = ::std::uint32_t{}; index < memory_properties.memoryTypeCount; ++index)
		{
			if ((requirements.memoryTypeBits & (1u << index)) != 0
				&& (memory_properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
			{
				allocate_info.memoryTypeIndex = index;
				found_memory_type = true;
				break;
			}
		}

		if (!found_memory_type)
		{
			throw ::std::runtime_error{"failed to find depth image memory type"};
		}

		auto depth_memory = VkDeviceMemory{};
		if (::vkAllocateMemory(renderer._device.handle, &allocate_info, nullptr, &depth_memory) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to allocate depth image memory"};
		}
		slot.depth_image._memory = ::vkkl::device_memory{renderer._device.handle, depth_memory};

		if (::vkBindImageMemory(renderer._device.handle, slot.depth_image._image.handle, slot.depth_image._memory.handle, 0) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to bind depth image memory"};
		}

		auto view_info = VkImageViewCreateInfo{};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.image = slot.depth_image._image.handle;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format = vulkan_context::_depth_format;
		view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.layerCount = 1;
		auto depth_view = VkImageView{};
		if (::vkCreateImageView(renderer._device.handle, &view_info, nullptr, &depth_view) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to create depth image view"};
		}
		slot.depth_image._view = ::vkkl::image_view{renderer._device.handle, depth_view};

		slot.depth_image._format = vulkan_context::_depth_format;
		slot.depth_image._extent = renderer._swapchain_extent;
	}

	++renderer._swapchain_revision;
}

static_assert(global_env_renderer<global_vulkan_env_renderer>);
static_assert(frame_env_renderer<frame_vulkan_env_renderer>);

NAGISA_BUILD_LIB_DETAIL_END

NAGISA_BUILD_LIB_BEGIN

using details::global_vulkan_env_renderer;
using details::frame_vulkan_env_renderer;
using details::vulkan_context;
using details::vulkan_image;
using details::frames_in_flight;
using details::resize;

NAGISA_BUILD_LIB_END

#include <nagisa/build_lib/destruct.h>
