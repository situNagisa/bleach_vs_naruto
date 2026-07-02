#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct query_pool_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkQueryPool handle = VK_NULL_HANDLE;
};

struct query_pool : query_pool_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr query_pool() noexcept = default;

	constexpr query_pool(::VkDevice device, ::VkQueryPool handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: query_pool_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	query_pool(query_pool const&) = delete;
	auto operator=(query_pool const&) -> query_pool& = delete;

	constexpr query_pool(query_pool&& other) noexcept
		: query_pool_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(query_pool&& other) noexcept
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

	~query_pool() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyQueryPool(device, handle, allocation_callbacks);
		}
	}
};
}
