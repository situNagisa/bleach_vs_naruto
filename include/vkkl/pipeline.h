#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct pipeline_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkPipeline handle = VK_NULL_HANDLE;
};

struct pipeline : pipeline_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr pipeline() noexcept = default;

	constexpr pipeline(::VkDevice device, ::VkPipeline handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: pipeline_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	pipeline(pipeline const&) = delete;
	auto operator=(pipeline const&) -> pipeline& = delete;

	constexpr pipeline(pipeline&& other) noexcept
		: pipeline_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(pipeline&& other) noexcept
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

	~pipeline() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyPipeline(device, handle, allocation_callbacks);
		}
	}
};
}
