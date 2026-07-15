#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <VkBootstrap.h>
#include <entt/entt.hpp>
#include <exec/async_scope.hpp>
#include <stdexec/execution.hpp>

#include <bvn/graphics/render_workflow.h>
#include <bvn/graphics/vulkan_renderer.h>
#include <bvn/platform/platform.h>

namespace
{
struct context
{
	context()
	{
		renderer._target_window = &window;

		//+ create instance
		{
			auto builder = ::vkb::InstanceBuilder{};
			builder.set_app_name("bvn");
			builder.set_engine_name("bvn");
			builder.require_api_version(1, 3, 0);
			builder.enable_extensions(window.required_vulkan_extensions());
			builder.request_validation_layers(true);

			auto instance_result = builder.build();
			if (!instance_result)
			{
				throw ::std::runtime_error{"failed to create Vulkan instance"};
			}

			renderer._bootstrap_instance = instance_result.value();
			renderer._instance = ::vkkl::instance{renderer._bootstrap_instance.instance};
			renderer._debug_messenger = ::vkkl::debug_utils_messenger{renderer._instance.handle, renderer._bootstrap_instance.debug_messenger};
		}

		//+ create surface
		{
			auto raw_surface = window.vulkan_surface(renderer._instance.handle);

			if (raw_surface == VK_NULL_HANDLE)
			{
				throw ::std::runtime_error{"failed to create Vulkan surface"};
			}
			renderer._surface = ::vkkl::surface{renderer._instance.handle, raw_surface};
		}

		//+ create device
		{
			auto features_13 = VkPhysicalDeviceVulkan13Features{};
			features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
			features_13.dynamicRendering = VK_TRUE;
			features_13.synchronization2 = VK_TRUE;

			auto selector = ::vkb::PhysicalDeviceSelector{renderer._bootstrap_instance, renderer._surface.handle};
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

			renderer._bootstrap_device = device_result.value();
			renderer._physical_device = renderer._bootstrap_device.physical_device.physical_device;
			renderer._device = ::vkkl::device{renderer._bootstrap_device.device};

			auto queue_result = renderer._bootstrap_device.get_queue(::vkb::QueueType::graphics);
			if (!queue_result)
			{
				throw ::std::runtime_error{"failed to get graphics queue"};
			}
			renderer._graphics_queue = queue_result.value();

			auto queue_family_result = renderer._bootstrap_device.get_queue_index(::vkb::QueueType::graphics);
			if (!queue_family_result)
			{
				throw ::std::runtime_error{"failed to get graphics queue family"};
			}
			renderer._graphics_queue_family = queue_family_result.value();

			auto present_queue_result = renderer._bootstrap_device.get_queue(::vkb::QueueType::present);
			if (!present_queue_result)
			{
				throw ::std::runtime_error{"failed to get present queue"};
			}
			renderer._present_queue = present_queue_result.value();

			auto present_queue_family_result = renderer._bootstrap_device.get_queue_index(::vkb::QueueType::present);
			if (!present_queue_family_result)
			{
				throw ::std::runtime_error{"failed to get present queue family"};
			}
			renderer._present_queue_family = present_queue_family_result.value();

			auto properties = VkPhysicalDeviceProperties{};
			::vkGetPhysicalDeviceProperties(renderer._physical_device, &properties);
			::std::memcpy(renderer._device_name, properties.deviceName, sizeof(renderer._device_name));
		}

		//+ create primary frame slots
		{
			auto pool_info = VkCommandPoolCreateInfo{};
			pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			pool_info.queueFamilyIndex = renderer._graphics_queue_family;

			auto semaphore_info = VkSemaphoreCreateInfo{};
			semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			auto fence_info = VkFenceCreateInfo{};
			fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

			for (auto&& slot : renderer._slots)
			{
				auto command_pool = VkCommandPool{};
				if (::vkCreateCommandPool(renderer._device.handle, &pool_info, nullptr, &command_pool) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create frame command pool"};
				}
				slot.primary_command_pool = ::vkkl::command_pool{renderer._device.handle, command_pool};

				auto command_allocate = VkCommandBufferAllocateInfo{};
				command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				command_allocate.commandPool = slot.primary_command_pool.handle;
				command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				command_allocate.commandBufferCount = 1;
				if (::vkAllocateCommandBuffers(renderer._device.handle, &command_allocate, &slot.primary_command_buffer) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to allocate frame command buffer"};
				}

				auto image_available = VkSemaphore{};
				if (::vkCreateSemaphore(renderer._device.handle, &semaphore_info, nullptr, &image_available) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create image-available semaphore"};
				}
				slot.image_available = ::vkkl::semaphore{renderer._device.handle, image_available};

				auto in_flight = VkFence{};
				if (::vkCreateFence(renderer._device.handle, &fence_info, nullptr, &in_flight) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to create in-flight fence"};
				}
				slot.in_flight = ::vkkl::fence{renderer._device.handle, in_flight};
			}
		}

		::bvn::graphics::resize(renderer, window.drawable_extent());
	}

	~context() noexcept
	{
		try
		{
			render_workflow.wait_for_submit();
		}
		catch (...)
		{
		}

		if (renderer._device.handle != VK_NULL_HANDLE)
		{
			auto const result = ::vkDeviceWaitIdle(renderer._device.handle);
			if (result != ::VK_SUCCESS && result != ::VK_ERROR_DEVICE_LOST)
			{
				::std::terminate();
			}
		}
		render_workflow.complete_submissions();

		render_scope.request_stop();
		render_workflow.request_stop();

		try
		{
			render_workflow.submit();
		}
		catch (...)
		{
		}

		main_scope.request_stop();

		try
		{
			::stdexec::sync_wait(render_scope.on_empty());
		}
		catch (...)
		{
		}

		try
		{
			::stdexec::sync_wait(main_scope.on_empty());
		}
		catch (...)
		{
		}

		if (renderer._device.handle != VK_NULL_HANDLE)
		{
			auto const result = ::vkDeviceWaitIdle(renderer._device.handle);
			if (result != ::VK_SUCCESS && result != ::VK_ERROR_DEVICE_LOST)
			{
				::std::terminate();
			}
		}

		main_scheduler.finish();
		render_workflow.finish();
	}

	context(context const&) = delete;
	auto operator=(context const&) -> context& = delete;

	context(context&&) = delete;
	auto operator=(context&&) -> context& = delete;

	::bvn::platform::event_state events{};
	::std::mutex events_mutex{};
	::std::uint64_t events_revision = 0;
	::std::chrono::milliseconds frame_time{};
	::entt::registry registry{};

	::bvn::platform::sdl_context sdl{};
	::bvn::platform::window window{"bvn m1", 1280, 720};
	::bvn::graphics::vulkan_context renderer{};

	::bvn::graphics::render_workflow render_workflow{renderer};
	::exec::async_scope render_scope{};

	::stdexec::run_loop main_scheduler{};
	::exec::async_scope main_scope{};
	::std::jthread main_thread{[this] { main_scheduler.run(); }};
};
}
