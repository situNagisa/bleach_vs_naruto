#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct descriptor_set_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	::VkDescriptorSet handle = VK_NULL_HANDLE;
};

struct descriptor_set : descriptor_set_observer
{
	constexpr descriptor_set() noexcept = default;

	constexpr descriptor_set(::VkDevice device, ::VkDescriptorPool descriptor_pool, ::VkDescriptorSet handle) noexcept
		: descriptor_set_observer{.device = device, .descriptor_pool = descriptor_pool, .handle = handle}
	{}

	descriptor_set(descriptor_set const&) = delete;
	auto operator=(descriptor_set const&) -> descriptor_set& = delete;

	constexpr descriptor_set(descriptor_set&& other) noexcept
		: descriptor_set_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.descriptor_pool = ::std::exchange(other.descriptor_pool, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
	{}

	constexpr auto&& operator=(descriptor_set&& other) noexcept
	{
		if (this != &other)
		{
			[[maybe_unused]] auto temp = ::std::move(*this);
			device = ::std::exchange(other.device, VK_NULL_HANDLE);
			descriptor_pool = ::std::exchange(other.descriptor_pool, VK_NULL_HANDLE);
			handle = ::std::exchange(other.handle, VK_NULL_HANDLE);
		}

		return *this;
	}

	~descriptor_set() noexcept
	{
		if (device != VK_NULL_HANDLE && descriptor_pool != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			(void)::vkFreeDescriptorSets(device, descriptor_pool, 1, &handle);
		}
	}
};
}
