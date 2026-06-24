#include <algorithm>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

#include <VkBootstrap.h>
#include <vulkan/vulkan.h>

#include <bvn/rhi/vulkan_context.h>

namespace bvn::rhi
{
	namespace
	{
		void check(VkResult result, char const* message)
		{
			if (result != VK_SUCCESS)
			{
				throw std::runtime_error(std::string{message} + " VkResult=" + std::to_string(static_cast<int>(result)));
			}
		}

		template <typename result_type>
		void check_vkb(result_type const& result, char const* message)
		{
			if (!result)
			{
				auto error = result.full_error();
				auto text = std::string{message} + ": " + error.type.message();

				if (error.vk_result != VK_SUCCESS)
				{
					text += " VkResult=" + std::to_string(static_cast<int>(error.vk_result));
				}

				for (auto const& reason : error.detailed_failure_reasons)
				{
					text += "\n";
					text += reason;
				}

				throw std::runtime_error{text};
			}
		}
	}

	auto recording_task::promise_type::get_return_object() noexcept -> recording_task
	{
		return {};
	}

	auto recording_task::promise_type::initial_suspend() const noexcept -> std::suspend_never
	{
		return {};
	}

	auto recording_task::promise_type::final_suspend() const noexcept -> std::suspend_never
	{
		return {};
	}

	void recording_task::promise_type::return_void() const noexcept
	{
	}

	void recording_task::promise_type::unhandled_exception() const
	{
		throw;
	}

	vulkan_context::vulkan_context(vulkan_context_create_info create_info)
	{
		try
		{
			if (!create_info.create_surface)
			{
				throw std::runtime_error{"vulkan surface factory is empty"};
			}

			create_instance(create_info.instance_extensions, create_info.enable_validation);
			surface = create_info.create_surface(instance);

			if (surface == VK_NULL_HANDLE)
			{
				throw std::runtime_error{"failed to create Vulkan surface"};
			}

			create_device();
			create_swapchain(create_info.initial_extent);
			create_frame_resources();
		}
		catch (...)
		{
			destroy_frame_resources();
			destroy_swapchain();
			destroy_device();
			destroy_instance();
			throw;
		}
	}

	vulkan_context::~vulkan_context()
	{
		if (device != VK_NULL_HANDLE)
		{
			(void)vkDeviceWaitIdle(device);
		}

		destroy_frame_resources();
		destroy_swapchain();
		destroy_device();
		destroy_instance();
	}

	auto vulkan_context::swapchain_revision() const noexcept -> std::uint64_t
	{
		return current_swapchain_revision;
	}

	void vulkan_context::resize(surface_extent new_extent)
	{
		if (new_extent.width == 0 || new_extent.height == 0)
		{
			return;
		}

		wait_idle();
		destroy_swapchain();
		create_swapchain(new_extent);
	}

	auto vulkan_context::begin_frame() -> frame_context
	{
		if (frame_active)
		{
			throw std::runtime_error{"Vulkan frame already active"};
		}

		auto& frame = frames[current_frame];
		check(vkWaitForFences(device, 1, &frame.in_flight, VK_TRUE, UINT64_MAX), "failed to wait for frame fence");

		auto image_index = std::uint32_t{};
		auto acquire_result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frame.image_available, VK_NULL_HANDLE, &image_index);

		if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			resize({swapchain_extent.width, swapchain_extent.height});
			return {};
		}

		if (acquire_result != VK_SUBOPTIMAL_KHR)
		{
			check(acquire_result, "failed to acquire swapchain image");
		}

		check(vkResetFences(device, 1, &frame.in_flight), "failed to reset frame fence");
		check(vkResetCommandBuffer(frame.command_buffer, 0), "failed to reset command buffer");

		auto begin_info = VkCommandBufferBeginInfo{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		check(vkBeginCommandBuffer(frame.command_buffer, &begin_info), "failed to begin command buffer");

		active_image_index = image_index;
		frame_active = true;

		return frame_context
		{
			.command_buffer = frame.command_buffer,
			.swapchain_image = swapchain_images[image_index],
			.swapchain_image_view = swapchain_image_views[image_index],
			.image_index = image_index,
			.color_format = swapchain_image_format,
			.extent = swapchain_extent,
		};
	}

	void vulkan_context::end_frame()
	{
		if (!frame_active)
		{
			return;
		}

		auto& frame = frames[current_frame];
		check(vkEndCommandBuffer(frame.command_buffer), "failed to end command buffer");

		auto wait_stage = VkPipelineStageFlags{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
		auto submit_info = VkSubmitInfo{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &frame.image_available;
		submit_info.pWaitDstStageMask = &wait_stage;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &frame.command_buffer;
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &frame.render_finished;

		check(vkQueueSubmit(graphics_queue, 1, &submit_info, frame.in_flight), "failed to submit graphics queue");

		auto present_info = VkPresentInfoKHR{};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = &frame.render_finished;
		present_info.swapchainCount = 1;
		present_info.pSwapchains = &swapchain;
		present_info.pImageIndices = &active_image_index;

		auto present_result = vkQueuePresentKHR(present_queue, &present_info);

		frame_active = false;

		if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
		{
			resize({swapchain_extent.width, swapchain_extent.height});
		}
		else
		{
			check(present_result, "failed to present swapchain image");
		}

		current_frame = (current_frame + 1) % static_cast<std::uint32_t>(frames.size());
	}

	void vulkan_context::wait_idle() const
	{
		if (device != VK_NULL_HANDLE)
		{
			check(vkDeviceWaitIdle(device), "failed to wait for Vulkan device idle");
		}
	}

	void vulkan_context::transition_image(VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags aspect, VkImageLayout old_layout, VkImageLayout new_layout) const noexcept
	{
		auto barrier = VkImageMemoryBarrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = old_layout;
		barrier.newLayout = new_layout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = aspect;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		auto source_stage = VkPipelineStageFlags{VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
		auto destination_stage = VkPipelineStageFlags{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

		if (new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
		{
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = 0;
			source_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			destination_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		}
		else if (new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		}
		else if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		}

		vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	void vulkan_context::set_full_viewport_and_scissor(VkCommandBuffer command_buffer) const noexcept
	{
		auto viewport = VkViewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(swapchain_extent.width);
		viewport.height = static_cast<float>(swapchain_extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		auto scissor = VkRect2D{};
		scissor.offset = {0, 0};
		scissor.extent = swapchain_extent;

		vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);
	}

	void vulkan_context::create_instance(std::vector<char const*> const& extensions, bool enable_validation)
	{
		auto builder = vkb::InstanceBuilder{};
		builder.set_app_name("bvn");
		builder.set_engine_name("bvn");
		builder.require_api_version(1, 3, 0);
		builder.enable_extensions(extensions);
		builder.request_validation_layers(enable_validation);

		auto instance_result = builder.build();
		check_vkb(instance_result, "failed to create Vulkan instance");

		bootstrap_instance = instance_result.value();
		instance = bootstrap_instance.instance;
		debug_messenger = bootstrap_instance.debug_messenger;
	}

	void vulkan_context::create_device()
	{
		auto features_13 = VkPhysicalDeviceVulkan13Features{};
		features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		features_13.dynamicRendering = VK_TRUE;

		auto selector = vkb::PhysicalDeviceSelector{bootstrap_instance, surface};
		selector.set_minimum_version(1, 3);
		selector.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete);
		selector.allow_any_gpu_device_type(true);
		selector.set_required_features_13(features_13);

		auto physical_device_result = selector.select();
		check_vkb(physical_device_result, "failed to select Vulkan physical device");

		auto device_builder = vkb::DeviceBuilder{physical_device_result.value()};
		auto device_result = device_builder.build();
		check_vkb(device_result, "failed to create Vulkan device");

		bootstrap_device = device_result.value();
		device = bootstrap_device.device;
		physical_device = bootstrap_device.physical_device.physical_device;

		auto queue_result = bootstrap_device.get_queue(vkb::QueueType::graphics);
		check_vkb(queue_result, "failed to get graphics queue");
		graphics_queue = queue_result.value();

		auto queue_family_result = bootstrap_device.get_queue_index(vkb::QueueType::graphics);
		check_vkb(queue_family_result, "failed to get graphics queue family");
		graphics_queue_family = queue_family_result.value();

		auto present_queue_result = bootstrap_device.get_queue(vkb::QueueType::present);
		check_vkb(present_queue_result, "failed to get present queue");
		present_queue = present_queue_result.value();

		auto present_queue_family_result = bootstrap_device.get_queue_index(vkb::QueueType::present);
		check_vkb(present_queue_family_result, "failed to get present queue family");
		present_queue_family = present_queue_family_result.value();

		auto properties = VkPhysicalDeviceProperties{};
		vkGetPhysicalDeviceProperties(physical_device, &properties);
		std::memcpy(device_name, properties.deviceName, sizeof(device_name));
	}

	void vulkan_context::create_swapchain(surface_extent requested_extent)
	{
		auto width = std::max(requested_extent.width, 1u);
		auto height = std::max(requested_extent.height, 1u);

		auto swapchain_builder = vkb::SwapchainBuilder{physical_device, device, surface, graphics_queue_family, present_queue_family};
		swapchain_builder.set_desired_extent(width, height);
		swapchain_builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
		swapchain_builder.set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
		swapchain_builder.add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

		auto swapchain_result = swapchain_builder.build();
		check_vkb(swapchain_result, "failed to create Vulkan swapchain");

		auto created_swapchain = swapchain_result.value();
		swapchain = created_swapchain.swapchain;
		swapchain_image_format = created_swapchain.image_format;
		swapchain_extent = created_swapchain.extent;

		auto image_result = created_swapchain.get_images();
		check_vkb(image_result, "failed to get swapchain images");
		swapchain_images = image_result.value();

		auto image_view_result = created_swapchain.get_image_views();
		check_vkb(image_view_result, "failed to get swapchain image views");
		swapchain_image_views = image_view_result.value();
		++current_swapchain_revision;
	}

	void vulkan_context::create_frame_resources()
	{
		frames.resize(2);

		for (auto&& frame : frames)
		{
			auto pool_info = VkCommandPoolCreateInfo{};
			pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			pool_info.queueFamilyIndex = graphics_queue_family;
			check(vkCreateCommandPool(device, &pool_info, nullptr, &frame.command_pool), "failed to create command pool");

			auto buffer_info = VkCommandBufferAllocateInfo{};
			buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			buffer_info.commandPool = frame.command_pool;
			buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			buffer_info.commandBufferCount = 1;
			check(vkAllocateCommandBuffers(device, &buffer_info, &frame.command_buffer), "failed to allocate command buffer");

			auto semaphore_info = VkSemaphoreCreateInfo{};
			semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			check(vkCreateSemaphore(device, &semaphore_info, nullptr, &frame.image_available), "failed to create image-available semaphore");
			check(vkCreateSemaphore(device, &semaphore_info, nullptr, &frame.render_finished), "failed to create render-finished semaphore");

			auto fence_info = VkFenceCreateInfo{};
			fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			check(vkCreateFence(device, &fence_info, nullptr, &frame.in_flight), "failed to create frame fence");
		}
	}

	void vulkan_context::destroy_frame_resources() noexcept
	{
		for (auto&& frame : frames)
		{
			if (frame.in_flight != VK_NULL_HANDLE)
			{
				vkDestroyFence(device, frame.in_flight, nullptr);
			}

			if (frame.render_finished != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device, frame.render_finished, nullptr);
			}

			if (frame.image_available != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device, frame.image_available, nullptr);
			}

			if (frame.command_pool != VK_NULL_HANDLE)
			{
				vkDestroyCommandPool(device, frame.command_pool, nullptr);
			}
		}

		frames.clear();
		current_frame = 0;
		active_image_index = 0;
		frame_active = false;
	}

	void vulkan_context::destroy_swapchain() noexcept
	{
		for (auto image_view : swapchain_image_views)
		{
			vkDestroyImageView(device, image_view, nullptr);
		}

		swapchain_image_views.clear();
		swapchain_images.clear();

		if (swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(device, swapchain, nullptr);
			swapchain = VK_NULL_HANDLE;
		}
	}

	void vulkan_context::destroy_device() noexcept
	{
		if (device != VK_NULL_HANDLE)
		{
			vkb::destroy_device(bootstrap_device);
			device = VK_NULL_HANDLE;
			physical_device = VK_NULL_HANDLE;
			graphics_queue = VK_NULL_HANDLE;
			present_queue = VK_NULL_HANDLE;
		}
	}

	void vulkan_context::destroy_instance() noexcept
	{
		if (surface != VK_NULL_HANDLE)
		{
			vkb::destroy_surface(bootstrap_instance, surface);
			surface = VK_NULL_HANDLE;
		}

		if (debug_messenger != VK_NULL_HANDLE)
		{
			vkb::destroy_debug_utils_messenger(instance, debug_messenger);
			debug_messenger = VK_NULL_HANDLE;
		}

		if (instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(instance, nullptr);
			instance = VK_NULL_HANDLE;
		}
	}
}
