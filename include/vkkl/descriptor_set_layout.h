#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct descriptor_set_layout_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkDescriptorSetLayout handle = VK_NULL_HANDLE;
};

struct descriptor_set_layout : descriptor_set_layout_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr descriptor_set_layout() noexcept = default;

	constexpr descriptor_set_layout(::VkDevice device, ::VkDescriptorSetLayout handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: descriptor_set_layout_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	descriptor_set_layout(descriptor_set_layout const&) = delete;
	auto operator=(descriptor_set_layout const&) -> descriptor_set_layout& = delete;

	constexpr descriptor_set_layout(descriptor_set_layout&& other) noexcept
		: descriptor_set_layout_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(descriptor_set_layout&& other) noexcept
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

	~descriptor_set_layout() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyDescriptorSetLayout(device, handle, allocation_callbacks);
		}
	}
};
}
