#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct fence_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkFence handle = VK_NULL_HANDLE;
};

struct fence : fence_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr fence() noexcept = default;

	constexpr fence(::VkDevice device, ::VkFence handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: fence_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	fence(fence const&) = delete;
	auto operator=(fence const&) -> fence& = delete;

	constexpr fence(fence&& other) noexcept
		: fence_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(fence&& other) noexcept
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

	~fence() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyFence(device, handle, allocation_callbacks);
		}
	}
};
}
