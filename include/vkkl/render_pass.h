#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct render_pass_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkRenderPass handle = VK_NULL_HANDLE;
};

struct render_pass : render_pass_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr render_pass() noexcept = default;

	constexpr render_pass(::VkDevice device, ::VkRenderPass handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: render_pass_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	render_pass(render_pass const&) = delete;
	auto operator=(render_pass const&) -> render_pass& = delete;

	constexpr render_pass(render_pass&& other) noexcept
		: render_pass_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(render_pass&& other) noexcept
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

	~render_pass() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyRenderPass(device, handle, allocation_callbacks);
		}
	}
};
}
