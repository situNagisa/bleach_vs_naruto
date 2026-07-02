#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct debug_utils_messenger_observer
{
	::VkInstance instance = VK_NULL_HANDLE;
	::VkDebugUtilsMessengerEXT handle = VK_NULL_HANDLE;
};

struct debug_utils_messenger : debug_utils_messenger_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr debug_utils_messenger() noexcept = default;

	constexpr debug_utils_messenger(::VkInstance instance, ::VkDebugUtilsMessengerEXT handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: debug_utils_messenger_observer{.instance = instance, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	debug_utils_messenger(debug_utils_messenger const&) = delete;
	auto operator=(debug_utils_messenger const&) -> debug_utils_messenger& = delete;

	constexpr debug_utils_messenger(debug_utils_messenger&& other) noexcept
		: debug_utils_messenger_observer
		{
			.instance = ::std::exchange(other.instance, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(debug_utils_messenger&& other) noexcept
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

	~debug_utils_messenger() noexcept
	{
		if (instance != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			auto destroy = reinterpret_cast<::PFN_vkDestroyDebugUtilsMessengerEXT>(::vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
			if (destroy != nullptr)
			{
				destroy(instance, handle, allocation_callbacks);
			}
		}
	}
};
}
