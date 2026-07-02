#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct command_pool_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkCommandPool handle = VK_NULL_HANDLE;
};

struct command_pool : command_pool_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr command_pool() noexcept = default;

	constexpr command_pool(::VkDevice device, ::VkCommandPool handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: command_pool_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	command_pool(command_pool const&) = delete;
	auto operator=(command_pool const&) -> command_pool& = delete;

	constexpr command_pool(command_pool&& other) noexcept
		: command_pool_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(command_pool&& other) noexcept
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

	~command_pool() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyCommandPool(device, handle, allocation_callbacks);
		}
	}
};
}
