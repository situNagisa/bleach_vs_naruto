#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace vkkl
{
struct shader_module_observer
{
	::VkDevice device = VK_NULL_HANDLE;
	::VkShaderModule handle = VK_NULL_HANDLE;
};

struct shader_module : shader_module_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr shader_module() noexcept = default;

	constexpr shader_module(::VkDevice device, ::VkShaderModule handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: shader_module_observer{.device = device, .handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	shader_module(shader_module const&) = delete;
	auto operator=(shader_module const&) -> shader_module& = delete;

	constexpr shader_module(shader_module&& other) noexcept
		: shader_module_observer
		{
			.device = ::std::exchange(other.device, VK_NULL_HANDLE),
			.handle = ::std::exchange(other.handle, VK_NULL_HANDLE),
		}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(shader_module&& other) noexcept
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

	~shader_module() noexcept
	{
		if (device != VK_NULL_HANDLE && handle != VK_NULL_HANDLE)
		{
			::vkDestroyShaderModule(device, handle, allocation_callbacks);
		}
	}
};
}
