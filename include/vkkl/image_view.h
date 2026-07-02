#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct image_view_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkImageView handle = VK_NULL_HANDLE;
};

struct image_view : image_view_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr image_view() noexcept = default;

	constexpr image_view(::VkDevice device, ::VkImageView handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: image_view_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	image_view(image_view const&) = delete;
	auto operator=(image_view const&) -> image_view& = delete;

	constexpr image_view(image_view&& other) noexcept
		: image_view_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(image_view&& other) noexcept
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

	~image_view() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyImageView(device, handle, allocation_callbacks);
		}
	}
};
}
