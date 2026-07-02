#pragma once

#include <cstdint>
#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct swapchain_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkSwapchainKHR handle = VK_NULL_HANDLE;

	decltype(auto) acquire_next_image_khr(::std::uint64_t timeout, ::VkSemaphore semaphore, ::VkFence fence, ::std::uint32_t& image_index) const noexcept
	{
		return ::vkAcquireNextImageKHR(device, handle, timeout, semaphore, fence, &image_index);
	}
};

struct swapchain : swapchain_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr swapchain() noexcept = default;

	constexpr swapchain(::VkDevice device, ::VkSwapchainKHR handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: swapchain_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	swapchain(swapchain const&) = delete;
	auto operator=(swapchain const&) -> swapchain& = delete;

	constexpr swapchain(swapchain&& other) noexcept
		: swapchain_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(swapchain&& other) noexcept
	{
		if (this != &other)
		{
			[[maybe_unused]] auto temp = ::std::move(*this);
			device = ::std::exchange(other.device, VK_NULL_HANDLE);
			handle = ::std::exchange(other.handle, VK_NULL_HANDLE);
			allocation_callbacks = ::std::exchange(other.allocation_callbacks, nullptr);
		}

		return *this;
	}

	~swapchain() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroySwapchainKHR(device, handle, allocation_callbacks);
		}
	}
};
}
