#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <VkBootstrap.h>

#include <bvn/renderer/vulkan_renderer.h>

namespace bvn::renderer
{
vulkan_renderer::vulkan_renderer(::bvn::platform::window const& window, bool enable_validation)
{
	target_window = &window;

	try
	{
		//+ create instance
		{
			auto builder = ::vkb::InstanceBuilder{};
			builder.set_app_name("bvn");
			builder.set_engine_name("bvn");
			builder.require_api_version(1, 3, 0);
			builder.enable_extensions(window.required_vulkan_extensions());
			builder.request_validation_layers(enable_validation);

			auto instance_result = builder.build();
			if (!instance_result)
			{
				throw ::std::runtime_error{"failed to create Vulkan instance"};
			}

			bootstrap_instance = instance_result.value();
			instance = ::vkkl::instance{bootstrap_instance.instance};
			debug_messenger = ::vkkl::debug_utils_messenger{instance.handle, bootstrap_instance.debug_messenger};
		}

		//+ create surface
		{
			auto raw_surface = window.vulkan_surface(instance.handle);

			if (raw_surface == VK_NULL_HANDLE)
			{
				throw ::std::runtime_error{"failed to create Vulkan surface"};
			}
			surface = ::vkkl::surface{instance.handle, raw_surface};
		}

		//+ create device
		{
			auto features_13 = VkPhysicalDeviceVulkan13Features{};
			features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
			features_13.dynamicRendering = VK_TRUE;
			features_13.synchronization2 = VK_TRUE;

			auto selector = ::vkb::PhysicalDeviceSelector{bootstrap_instance, surface.handle};
			selector.set_minimum_version(1, 3);
			selector.prefer_gpu_device_type(::vkb::PreferredDeviceType::discrete);
			selector.allow_any_gpu_device_type(true);
			selector.set_required_features_13(features_13);

			auto physical_device_result = selector.select();
			if (!physical_device_result)
			{
				throw ::std::runtime_error{"failed to select Vulkan physical device"};
			}

			auto device_builder = ::vkb::DeviceBuilder{physical_device_result.value()};
			auto device_result = device_builder.build();
			if (!device_result)
			{
				throw ::std::runtime_error{"failed to create Vulkan device"};
			}

			bootstrap_device = device_result.value();
			physical_device = bootstrap_device.physical_device.physical_device;
			device = ::vkkl::device{bootstrap_device.device};

			auto queue_result = bootstrap_device.get_queue(::vkb::QueueType::graphics);
			if (!queue_result)
			{
				throw ::std::runtime_error{"failed to get graphics queue"};
			}
			graphics_queue = queue_result.value();

			auto queue_family_result = bootstrap_device.get_queue_index(::vkb::QueueType::graphics);
			if (!queue_family_result)
			{
				throw ::std::runtime_error{"failed to get graphics queue family"};
			}
			graphics_queue_family = queue_family_result.value();

			auto present_queue_result = bootstrap_device.get_queue(::vkb::QueueType::present);
			if (!present_queue_result)
			{
				throw ::std::runtime_error{"failed to get present queue"};
			}
			present_queue = present_queue_result.value();

			auto present_queue_family_result = bootstrap_device.get_queue_index(::vkb::QueueType::present);
			if (!present_queue_family_result)
			{
				throw ::std::runtime_error{"failed to get present queue family"};
			}
			present_queue_family = present_queue_family_result.value();

			auto properties = VkPhysicalDeviceProperties{};
			::vkGetPhysicalDeviceProperties(physical_device, &properties);
			::std::memcpy(device_name, properties.deviceName, sizeof(device_name));
		}

		//+ create swapchain, image views, and per-image present semaphores
		{
			auto requested_extent = window.drawable_extent();
			auto swapchain_builder = ::vkb::SwapchainBuilder{physical_device, device.handle, surface.handle, graphics_queue_family, present_queue_family};
			swapchain_builder.set_desired_extent(::std::max(requested_extent.width, 1u), ::std::max(requested_extent.height, 1u));
			swapchain_builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
			swapchain_builder.set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
			swapchain_builder.add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

			auto swapchain_result = swapchain_builder.build();
			if (!swapchain_result)
			{
				throw ::std::runtime_error{"failed to create Vulkan swapchain"};
			}

			auto created_swapchain = swapchain_result.value();
			swapchain = ::vkkl::swapchain{device.handle, created_swapchain.swapchain};
			swapchain_image_format = created_swapchain.image_format;
			swapchain_extent = created_swapchain.extent;

			auto image_result = created_swapchain.get_images();
			if (!image_result)
			{
				throw ::std::runtime_error{"failed to get swapchain images"};
			}
			swapchain_images = image_result.value();

			auto next_image_views = ::std::vector<::vkkl::image_view>{};
			next_image_views.reserve(swapchain_images.size());
			for (auto image : swapchain_images)
			{
				auto image_view_info = VkImageViewCreateInfo{};
				image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				image_view_info.image = image;
				image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
				image_view_info.format = swapchain_image_format;
				image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				image_view_info.subresourceRange.baseMipLevel = 0;
				image_view_info.subresourceRange.levelCount = 1;
				image_view_info.subresourceRange.baseArrayLayer = 0;
				image_view_info.subresourceRange.layerCount = 1;

				auto image_view = VkImageView{};
				if (::vkCreateImageView(device.handle, &image_view_info, nullptr, &image_view) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create swapchain image view"};
				}
				next_image_views.emplace_back(device.handle, image_view);
			}
			swapchain_image_views = ::std::move(next_image_views);

			auto semaphore_info = VkSemaphoreCreateInfo{};
			semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			render_finished.clear();
			render_finished.reserve(swapchain_images.size());

			for ([[maybe_unused]] auto image : swapchain_images)
			{
				auto semaphore = VkSemaphore{};
				if (::vkCreateSemaphore(device.handle, &semaphore_info, nullptr, &semaphore) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create per-image render-finished semaphore"};
				}
				render_finished.emplace_back(device.handle, semaphore);
			}

			++swapchain_revision;
		}

		//+ create frame slots and per-slot depth images
		{
			auto pool_info = VkCommandPoolCreateInfo{};
			pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			pool_info.queueFamilyIndex = graphics_queue_family;

			auto semaphore_info = VkSemaphoreCreateInfo{};
			semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			auto fence_info = VkFenceCreateInfo{};
			fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

			for (auto&& slot : slots)
			{
				auto command_pool = VkCommandPool{};
				if (::vkCreateCommandPool(device.handle, &pool_info, nullptr, &command_pool) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create frame command pool"};
				}
				slot.command_pool = ::vkkl::command_pool{device.handle, command_pool};

				auto command_allocate = VkCommandBufferAllocateInfo{};
				command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				command_allocate.commandPool = slot.command_pool.handle;
				command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				command_allocate.commandBufferCount = 1;
				if (::vkAllocateCommandBuffers(device.handle, &command_allocate, &slot.command_buffer) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to allocate frame command buffer"};
				}

				auto image_available = VkSemaphore{};
				if (::vkCreateSemaphore(device.handle, &semaphore_info, nullptr, &image_available) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create image-available semaphore"};
				}
				slot.image_available = ::vkkl::semaphore{device.handle, image_available};

				auto in_flight = VkFence{};
				if (::vkCreateFence(device.handle, &fence_info, nullptr, &in_flight) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create in-flight fence"};
				}
				slot.in_flight = ::vkkl::fence{device.handle, in_flight};

				auto image_info = VkImageCreateInfo{};
				image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				image_info.imageType = VK_IMAGE_TYPE_2D;
				image_info.extent = {swapchain_extent.width, swapchain_extent.height, 1};
				image_info.mipLevels = 1;
				image_info.arrayLayers = 1;
				image_info.format = depth_format;
				image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
				image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
				image_info.samples = VK_SAMPLE_COUNT_1_BIT;
				image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				auto depth_image = VkImage{};
				if (::vkCreateImage(device.handle, &image_info, nullptr, &depth_image) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create depth image"};
				}
				slot.depth_image.image = ::vkkl::image{device.handle, depth_image};

				auto requirements = VkMemoryRequirements{};
				::vkGetImageMemoryRequirements(device.handle, slot.depth_image.image.handle, &requirements);

				auto allocate_info = VkMemoryAllocateInfo{};
				allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				allocate_info.allocationSize = requirements.size;

				auto memory_properties = VkPhysicalDeviceMemoryProperties{};
				::vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
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
				if (::vkAllocateMemory(device.handle, &allocate_info, nullptr, &depth_memory) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to allocate depth image memory"};
				}
				slot.depth_image.memory = ::vkkl::device_memory{device.handle, depth_memory};

				if (::vkBindImageMemory(device.handle, slot.depth_image.image.handle, slot.depth_image.memory.handle, 0) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to bind depth image memory"};
				}

				auto view_info = VkImageViewCreateInfo{};
				view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				view_info.image = slot.depth_image.image.handle;
				view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
				view_info.format = depth_format;
				view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				view_info.subresourceRange.baseMipLevel = 0;
				view_info.subresourceRange.levelCount = 1;
				view_info.subresourceRange.baseArrayLayer = 0;
				view_info.subresourceRange.layerCount = 1;
				auto depth_view = VkImageView{};
				if (::vkCreateImageView(device.handle, &view_info, nullptr, &depth_view) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create depth image view"};
				}
				slot.depth_image.view = ::vkkl::image_view{device.handle, depth_view};

				slot.depth_image.format = depth_format;
				slot.depth_image.extent = swapchain_extent;
			}
		}
	}
	catch (...)
	{
		if (device.handle != VK_NULL_HANDLE)
		{
			(void)::vkDeviceWaitIdle(device.handle);
		}

		//+ release Vulkan objects that depend on device
		{
				for (auto&& slot : slots)
				{
					slot.secondary_command_buffers.clear();
					slot.depth_image.view = {};
					slot.depth_image.image = {};
					slot.depth_image.memory = {};
					slot.depth_image.format = VK_FORMAT_UNDEFINED;
					slot.depth_image.extent = {};
				slot.in_flight = {};
				slot.image_available = {};
				slot.command_pool = {};
				slot.command_buffer = VK_NULL_HANDLE;
			}

			render_finished.clear();
			swapchain_image_views.clear();
			swapchain_images.clear();
			swapchain = {};
		}

		device = {};
		physical_device = VK_NULL_HANDLE;
		graphics_queue = VK_NULL_HANDLE;
		present_queue = VK_NULL_HANDLE;
		surface = {};
		debug_messenger = {};
		instance = {};

		throw;
	}
}

vulkan_renderer::~vulkan_renderer() noexcept
{
	if (device.handle != VK_NULL_HANDLE)
	{
		(void)::vkDeviceWaitIdle(device.handle);
	}

	//+ release Vulkan objects that depend on device
	{
			for (auto&& slot : slots)
			{
				slot.secondary_command_buffers.clear();
				slot.depth_image.view = {};
				slot.depth_image.image = {};
				slot.depth_image.memory = {};
				slot.depth_image.format = VK_FORMAT_UNDEFINED;
				slot.depth_image.extent = {};
			slot.in_flight = {};
			slot.image_available = {};
			slot.command_pool = {};
			slot.command_buffer = VK_NULL_HANDLE;
		}

		render_finished.clear();
		swapchain_image_views.clear();
		swapchain_images.clear();
		swapchain = {};
	}

	device = {};
	physical_device = VK_NULL_HANDLE;
	graphics_queue = VK_NULL_HANDLE;
	present_queue = VK_NULL_HANDLE;
	surface = {};
	debug_messenger = {};
	instance = {};
}

void vulkan_renderer::resize(::bvn::platform::window_extent new_extent)
{
	if (new_extent.width == 0 || new_extent.height == 0)
	{
		return;
	}

	if (device.handle != VK_NULL_HANDLE && ::vkDeviceWaitIdle(device.handle) != ::VK_SUCCESS)
	{
		throw ::std::runtime_error{"failed to wait for Vulkan device idle"};
	}

	//+ release swapchain-sized resources before rebuilding them
	{
		for (auto&& slot : slots)
		{
			slot.depth_image.view = {};
			slot.depth_image.image = {};
			slot.depth_image.memory = {};
			slot.depth_image.format = VK_FORMAT_UNDEFINED;
			slot.depth_image.extent = {};
		}

		render_finished.clear();
		swapchain_image_views.clear();
		swapchain_images.clear();
		swapchain = {};
	}

	auto swapchain_builder = ::vkb::SwapchainBuilder{physical_device, device.handle, surface.handle, graphics_queue_family, present_queue_family};
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
	swapchain = ::vkkl::swapchain{device.handle, created_swapchain.swapchain};
	swapchain_image_format = created_swapchain.image_format;
	swapchain_extent = created_swapchain.extent;

	auto image_result = created_swapchain.get_images();
	if (!image_result)
	{
		throw ::std::runtime_error{"failed to get swapchain images"};
	}
	swapchain_images = image_result.value();

	auto next_image_views = ::std::vector<::vkkl::image_view>{};
	next_image_views.reserve(swapchain_images.size());
	for (auto image : swapchain_images)
	{
		auto image_view_info = VkImageViewCreateInfo{};
		image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_info.image = image;
		image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		image_view_info.format = swapchain_image_format;
		image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_info.subresourceRange.baseMipLevel = 0;
		image_view_info.subresourceRange.levelCount = 1;
		image_view_info.subresourceRange.baseArrayLayer = 0;
		image_view_info.subresourceRange.layerCount = 1;

		auto image_view = VkImageView{};
		if (::vkCreateImageView(device.handle, &image_view_info, nullptr, &image_view) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to create swapchain image view"};
		}
		next_image_views.emplace_back(device.handle, image_view);
	}
	swapchain_image_views = ::std::move(next_image_views);

	auto semaphore_info = VkSemaphoreCreateInfo{};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	render_finished.clear();
	render_finished.reserve(swapchain_images.size());

	for ([[maybe_unused]] auto image : swapchain_images)
	{
		auto semaphore = VkSemaphore{};
		if (::vkCreateSemaphore(device.handle, &semaphore_info, nullptr, &semaphore) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to create per-image render-finished semaphore"};
		}
		render_finished.emplace_back(device.handle, semaphore);
	}

	for (auto&& slot : slots)
	{
		auto image_info = VkImageCreateInfo{};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.extent = {swapchain_extent.width, swapchain_extent.height, 1};
		image_info.mipLevels = 1;
		image_info.arrayLayers = 1;
		image_info.format = depth_format;
		image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		auto depth_image = VkImage{};
		if (::vkCreateImage(device.handle, &image_info, nullptr, &depth_image) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to create depth image"};
		}
		slot.depth_image.image = ::vkkl::image{device.handle, depth_image};

		auto requirements = VkMemoryRequirements{};
		::vkGetImageMemoryRequirements(device.handle, slot.depth_image.image.handle, &requirements);

		auto allocate_info = VkMemoryAllocateInfo{};
		allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocate_info.allocationSize = requirements.size;

		auto memory_properties = VkPhysicalDeviceMemoryProperties{};
		::vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
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
		if (::vkAllocateMemory(device.handle, &allocate_info, nullptr, &depth_memory) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to allocate depth image memory"};
		}
		slot.depth_image.memory = ::vkkl::device_memory{device.handle, depth_memory};

		if (::vkBindImageMemory(device.handle, slot.depth_image.image.handle, slot.depth_image.memory.handle, 0) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to bind depth image memory"};
		}

		auto view_info = VkImageViewCreateInfo{};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.image = slot.depth_image.image.handle;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format = depth_format;
		view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.layerCount = 1;
		auto depth_view = VkImageView{};
		if (::vkCreateImageView(device.handle, &view_info, nullptr, &depth_view) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to create depth image view"};
		}
		slot.depth_image.view = ::vkkl::image_view{device.handle, depth_view};

		slot.depth_image.format = depth_format;
		slot.depth_image.extent = swapchain_extent;
	}

	++swapchain_revision;
}

}
