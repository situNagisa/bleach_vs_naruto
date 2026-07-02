#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct buffer_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkBuffer handle = VK_NULL_HANDLE;
};

struct buffer : buffer_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr buffer() noexcept = default;

	constexpr buffer(::VkDevice device, ::VkBuffer handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: buffer_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	buffer(buffer const&) = delete;
	auto operator=(buffer const&) -> buffer& = delete;

	constexpr buffer(buffer&& other) noexcept
		: buffer_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(buffer&& other) noexcept
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

	~buffer() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyBuffer(device, handle, allocation_callbacks);
		}
	}
};
}
