#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct pipeline_cache_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkPipelineCache handle = VK_NULL_HANDLE;
};

struct pipeline_cache : pipeline_cache_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr pipeline_cache() noexcept = default;

	constexpr pipeline_cache(::VkDevice device, ::VkPipelineCache handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: pipeline_cache_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	pipeline_cache(pipeline_cache const&) = delete;
	auto operator=(pipeline_cache const&) -> pipeline_cache& = delete;

	constexpr pipeline_cache(pipeline_cache&& other) noexcept
		: pipeline_cache_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(pipeline_cache&& other) noexcept
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

	~pipeline_cache() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyPipelineCache(device, handle, allocation_callbacks);
		}
	}
};
}
