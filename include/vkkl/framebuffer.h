#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct framebuffer_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkFramebuffer handle = VK_NULL_HANDLE;
};

struct framebuffer : framebuffer_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr framebuffer() noexcept = default;

	constexpr framebuffer(::VkDevice device, ::VkFramebuffer handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: framebuffer_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	framebuffer(framebuffer const&) = delete;
	auto operator=(framebuffer const&) -> framebuffer& = delete;

	constexpr framebuffer(framebuffer&& other) noexcept
		: framebuffer_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(framebuffer&& other) noexcept
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

	~framebuffer() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyFramebuffer(device, handle, allocation_callbacks);
		}
	}
};
}
