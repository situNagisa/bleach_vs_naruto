#pragma once

#include <cassert>
#include <ranges>

#include <vkkl/vkkl.h>
#include <vkfu/generated/vulkan-v1.4.328.h>

#include "../detail/demo_vulkan.h"

struct buffer_type
{
	::vkkl::buffer handle{};
	::vkkl::device_memory memory{};

	buffer_type(::VkDevice device, ::VkPhysicalDevice physical_device, ::std::span<::std::byte const> data, ::vkfu::param::buffer::usage_type usage)
		: handle(device, ::vkfu::create_buffer(device, ::vkfu::param::buffer{ .size = data.size(), .usage = usage }))
		, memory([&]
			{
				auto req = ::vkfu::get_buffer_memory_requirements2(device, ::vkfu::param::buffer_memory_requirements2{ .buffer = handle.handle }).head().memoryRequirements;
				auto properties = ::vkfu::get_physical_device_memory_properties2(physical_device).head().memoryProperties;
				auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
				auto index = ::std::optional<::std::uint32_t>{};
				for (auto&& [i, type] : properties.memoryTypes | ::std::views::take(properties.memoryTypeCount) | ::std::views::enumerate)
				{
					if (req.memoryTypeBits & (1u << i) && (type.propertyFlags & flags) == flags)
					{
						index.emplace(i);
						break;
					}
				}
				if (!index)
					throw ::std::runtime_error{ "main menu: no compatible Vulkan memory type" };
				return ::vkkl::device_memory(device, ::vkfu::allocate_memory(device, ::vkfu::param::memory{ .allocation_size = req.size, .type_index = *index }));
			}())
	{
		namespace param = ::vkfu::param;
		assert(!data.empty());

		::vkfu::bind_buffer_memory2(device, ::std::array{ ::vkfu::evaluate(param::bind_buffer_memory{.buffer = handle.handle, .memory = memory.handle }) });
		auto mapped = static_cast<void*>(nullptr);
		::consumer_arch_vulkan::check(::vkMapMemory(device, memory.handle, 0, data.size(), 0, &mapped), "main menu: map buffer");
		::std::memcpy(mapped, data.data(), data.size());
		::vkUnmapMemory(device, memory.handle);
	}
};