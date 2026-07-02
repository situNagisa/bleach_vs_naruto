#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct surface_observer
{
	::VkInstance instance = VK_NULL_HANDLE;
	::VkSurfaceKHR handle = VK_NULL_HANDLE;
};

struct surface : surface_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr surface() noexcept = default;

	constexpr surface(::VkInstance instance, ::VkSurfaceKHR handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: surface_observer{.instance = instance, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	surface(surface const&) = delete;
	auto operator=(surface const&) -> surface& = delete;

	constexpr surface(surface&& other) noexcept
		: surface_observer
		{
			.instance = ::std::exchange(other.instance, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(surface&& other) noexcept
	{
		if (this != &other)
		{
			[[maybe_unused]] auto temp = ::std::move(*this);
			instance = ::std::exchange(other.instance, VK_NULL_HANDLE);
			handle = ::std::exchange(other.handle, VK_NULL_HANDLE);
			allocation_callbacks = ::std::exchange(other.allocation_callbacks, nullptr);
		}

		return *this;
	}

	~surface() noexcept
	{
		if (instance != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroySurfaceKHR(instance, handle, allocation_callbacks);
		}
	}
};
}
