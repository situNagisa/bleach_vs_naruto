#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct instance_observer
{
	::VkInstance handle = VK_NULL_HANDLE;
};

struct instance : instance_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr instance() noexcept = default;

	constexpr instance(::VkInstance handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: instance_observer{.handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	instance(instance const&) = delete;
	auto operator=(instance const&) -> instance& = delete;

	constexpr instance(instance&& other) noexcept
		: instance_observer{.handle = ::std::exchange(other.handle, VK_NULL_HANDLE)}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(instance&& other) noexcept
	{
		if (this != &other)
		{
			[[maybe_unused]] auto temp = ::std::move(*this);
			handle = ::std::exchange(other.handle, VK_NULL_HANDLE);
			allocation_callbacks = ::std::exchange(other.allocation_callbacks, nullptr);
		}

		return *this;
	}

	~instance() noexcept
	{
		if (handle != VK_NULL_HANDLE)
		{
			::vkDestroyInstance(handle, allocation_callbacks);
		}
	}
};
}
