#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct sampler_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkSampler handle = VK_NULL_HANDLE;
};

struct sampler : sampler_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr sampler() noexcept = default;

	constexpr sampler(::VkDevice device, ::VkSampler handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: sampler_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	sampler(sampler const&) = delete;
	auto operator=(sampler const&) -> sampler& = delete;

	constexpr sampler(sampler&& other) noexcept
		: sampler_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(sampler&& other) noexcept
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

	~sampler() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroySampler(device, handle, allocation_callbacks);
		}
	}
};
}
