#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct image_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkImage handle = VK_NULL_HANDLE;
};

struct image : image_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr image() noexcept = default;

	constexpr image(::VkDevice device, ::VkImage handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: image_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	image(image const&) = delete;
	auto operator=(image const&) -> image& = delete;

	constexpr image(image&& other) noexcept
		: image_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(image&& other) noexcept
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

	~image() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyImage(device, handle, allocation_callbacks);
		}
	}
};
}
