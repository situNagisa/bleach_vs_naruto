#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct event_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkEvent handle = VK_NULL_HANDLE;
};

struct event : event_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr event() noexcept = default;

	constexpr event(::VkDevice device, ::VkEvent handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: event_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	event(event const&) = delete;
	auto operator=(event const&) -> event& = delete;

	constexpr event(event&& other) noexcept
		: event_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(event&& other) noexcept
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

	~event() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyEvent(device, handle, allocation_callbacks);
		}
	}
};
}
