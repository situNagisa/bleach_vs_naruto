#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct buffer_view_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkBufferView handle = VK_NULL_HANDLE;
};

struct buffer_view : buffer_view_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr buffer_view() noexcept = default;

	constexpr buffer_view(::VkDevice device, ::VkBufferView handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: buffer_view_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	buffer_view(buffer_view const&) = delete;
	auto operator=(buffer_view const&) -> buffer_view& = delete;

	constexpr buffer_view(buffer_view&& other) noexcept
		: buffer_view_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(buffer_view&& other) noexcept
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

	~buffer_view() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyBufferView(device, handle, allocation_callbacks);
		}
	}
};
}
