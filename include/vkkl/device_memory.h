#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct device_memory_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkDeviceMemory handle = VK_NULL_HANDLE;

	decltype(auto) map_memory(::VkDeviceSize offset, ::VkDeviceSize size, ::VkMemoryMapFlags flags, void*& data) const noexcept
	{
		return ::vkMapMemory(device, handle, offset, size, flags, &data);
	}

	decltype(auto) unmap_memory() const noexcept
	{
		return ::vkUnmapMemory(device, handle);
	}
};

struct device_memory : device_memory_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr device_memory() noexcept = default;

	constexpr device_memory(::VkDevice device, ::VkDeviceMemory handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: device_memory_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	device_memory(device_memory const&) = delete;
	auto operator=(device_memory const&) -> device_memory& = delete;

	constexpr device_memory(device_memory&& other) noexcept
		: device_memory_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(device_memory&& other) noexcept
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

	~device_memory() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkFreeMemory(device, handle, allocation_callbacks);
		}
	}
};
}
