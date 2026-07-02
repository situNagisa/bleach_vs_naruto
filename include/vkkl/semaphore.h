#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct semaphore_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkSemaphore handle = VK_NULL_HANDLE;
};

struct semaphore : semaphore_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr semaphore() noexcept = default;

	constexpr semaphore(::VkDevice device, ::VkSemaphore handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: semaphore_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	semaphore(semaphore const&) = delete;
	auto operator=(semaphore const&) -> semaphore& = delete;

	constexpr semaphore(semaphore&& other) noexcept
		: semaphore_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(semaphore&& other) noexcept
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

	~semaphore() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroySemaphore(device, handle, allocation_callbacks);
		}
	}
};
}
