#pragma once

#include <expected>
#include <new>
#include <utility>

#include <vulkan/vulkan.h>

#include "device.h"

namespace vkkl
{
struct physical_device_observer
{
	::VkPhysicalDevice handle = VK_NULL_HANDLE;

	auto create_device(::VkDeviceCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_device(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_device(::VkDeviceCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<device, ::VkResult>
	{
		auto raw_device = ::VkDevice{};
		if (auto result = ::vkCreateDevice(handle, &create_info, allocation_callbacks, &raw_device); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return device{raw_device, allocation_callbacks};
	}
};
}
