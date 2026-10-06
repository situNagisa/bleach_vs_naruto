#pragma once

#include <vulkan/vulkan.h>
#include <vkkl/vkkl.h>
#include <vkfu/generated/vulkan-v1.4.328.h>

#include <nagisa/vkfu/execution_stream.h>

#include <stdexec/execution.hpp>

namespace my_impl
{

	[[nodiscard]] inline auto acquire(::VkDevice device, ::VkSwapchainKHR swapchain, ::std::span<::VkImage const> images)
	{
		return ::stdexec::just() | ::stdexec::then([device, swapchain, images]
			{
				auto semaphore = ::vkkl::semaphore{ device, ::vkfu::create_semaphore(device, ::vkfu::param::semaphore{}) };
				auto const index = ::vkfu::khr::acquire_next_image2(device, ::vkfu::param::khr::acquire_next_image{
					.swapchain = swapchain,
					.timeout = (::std::numeric_limits<::std::uint64_t>::max)(),
					.semaphore = semaphore.handle,
					.fence = VK_NULL_HANDLE,
					.device_mask = (::std::numeric_limits<::std::uint32_t>::max)(),
					});
				return acquired{
					{images[index], ::gpu::vk::undefined{}, ::std::exchange(semaphore.handle, VK_NULL_HANDLE), device},
					index,
				};
			});
	}

}
