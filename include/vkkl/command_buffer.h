#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct command_buffer_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkCommandPool command_pool = VK_NULL_HANDLE;
	::VkCommandBuffer handle = VK_NULL_HANDLE;
};

struct command_buffer : command_buffer_observer
{
	constexpr command_buffer() noexcept = default;

	constexpr command_buffer(::VkDevice device, ::VkCommandPool command_pool, ::VkCommandBuffer handle) noexcept
		: command_buffer_observer{.device = device, .command_pool = command_pool, .handle = handle}
	{}

	command_buffer(command_buffer const&) = delete;
	auto operator=(command_buffer const&) -> command_buffer& = delete;

	constexpr command_buffer(command_buffer&& other) noexcept
		: command_buffer_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.command_pool = ::std::exchange(other.command_pool, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
	{}

	constexpr auto&& operator=(command_buffer&& other) noexcept
	{
		if (this != &other)
		{
			[[maybe_unused]] auto temp = ::std::move(*this);
			device = ::std::exchange(other.device, VK_NULL_HANDLE);
			command_pool = ::std::exchange(other.command_pool, VK_NULL_HANDLE);
			handle = ::std::exchange(other.handle, VK_NULL_HANDLE);
		}

		return *this;
	}

	~command_buffer() noexcept
	{
		if (device != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkFreeCommandBuffers(device, command_pool, 1, &handle);
		}
	}
};
}
