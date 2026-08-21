#pragma once

#include <expected>
#include <new>
#include <utility>

#include <vulkan/vulkan.h>

#include "debug_utils_messenger.h"

namespace vkkl
{
struct instance_observer
{
	::VkInstance handle = VK_NULL_HANDLE;

	auto create_debug_utils_messenger_ext(::VkDebugUtilsMessageSeverityFlagsEXT message_severity, ::VkDebugUtilsMessageTypeFlagsEXT message_type, ::PFN_vkDebugUtilsMessengerCallbackEXT user_callback, void* user_data = nullptr, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_debug_utils_messenger_ext(message_severity, message_type, user_callback, user_data, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_debug_utils_messenger_ext(::VkDebugUtilsMessageSeverityFlagsEXT message_severity, ::VkDebugUtilsMessageTypeFlagsEXT message_type, ::PFN_vkDebugUtilsMessengerCallbackEXT user_callback, void* user_data, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<debug_utils_messenger, ::VkResult>
	{
		auto create = reinterpret_cast<::PFN_vkCreateDebugUtilsMessengerEXT>(::vkGetInstanceProcAddr(handle, "vkCreateDebugUtilsMessengerEXT"));
		if (create == nullptr) return ::std::unexpected{::VK_ERROR_EXTENSION_NOT_PRESENT};

		auto create_info = ::VkDebugUtilsMessengerCreateInfoEXT
		{
			.sType = ::VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
			.pNext = nullptr,
			.flags = {},
			.messageSeverity = message_severity,
			.messageType = message_type,
			.pfnUserCallback = user_callback,
			.pUserData = user_data,
		};
		auto raw_debug_utils_messenger = ::VkDebugUtilsMessengerEXT{};
		if (auto result = create(handle, &create_info, allocation_callbacks, &raw_debug_utils_messenger); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return debug_utils_messenger{handle, raw_debug_utils_messenger, allocation_callbacks};
	}
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
