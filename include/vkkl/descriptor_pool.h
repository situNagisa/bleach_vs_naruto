#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct descriptor_pool_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkDescriptorPool handle = VK_NULL_HANDLE;
};

struct descriptor_pool : descriptor_pool_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr descriptor_pool() noexcept = default;

	constexpr descriptor_pool(::VkDevice device, ::VkDescriptorPool handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: descriptor_pool_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	descriptor_pool(descriptor_pool const&) = delete;
	auto operator=(descriptor_pool const&) -> descriptor_pool& = delete;

	constexpr descriptor_pool(descriptor_pool&& other) noexcept
		: descriptor_pool_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(descriptor_pool&& other) noexcept
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

	~descriptor_pool() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyDescriptorPool(device, handle, allocation_callbacks);
		}
	}
};
}
