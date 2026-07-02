#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct pipeline_layout_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkPipelineLayout handle = VK_NULL_HANDLE;
};

struct pipeline_layout : pipeline_layout_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr pipeline_layout() noexcept = default;

	constexpr pipeline_layout(::VkDevice device, ::VkPipelineLayout handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: pipeline_layout_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	pipeline_layout(pipeline_layout const&) = delete;
	auto operator=(pipeline_layout const&) -> pipeline_layout& = delete;

	constexpr pipeline_layout(pipeline_layout&& other) noexcept
		: pipeline_layout_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(pipeline_layout&& other) noexcept
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

	~pipeline_layout() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyPipelineLayout(device, handle, allocation_callbacks);
		}
	}
};
}
