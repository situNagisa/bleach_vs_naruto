#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct device_observer
{
	::VkDevice handle = VK_NULL_HANDLE;
};

struct device : device_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr device() noexcept = default;

	constexpr device(::VkDevice handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: device_observer{.handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	device(device const&) = delete;
	auto operator=(device const&) -> device& = delete;

	constexpr device(device&& other) noexcept
		: device_observer{.handle = ::std::exchange(other.handle, VK_NULL_HANDLE)}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(device&& other) noexcept
	{
		if (this != &other)
		{
			[[maybe_unused]] auto temp = ::std::move(*this);
			handle = ::std::exchange(other.handle, VK_NULL_HANDLE);
			allocation_callbacks = ::std::exchange(other.allocation_callbacks, nullptr);
		}

		return *this;
	}

	~device() noexcept
	{
		if (handle != VK_NULL_HANDLE)
		{
			::vkDestroyDevice(handle, allocation_callbacks);
		}
	}
};
}
