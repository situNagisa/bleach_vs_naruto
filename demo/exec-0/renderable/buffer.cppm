module;

#include <cassert>

#include <vulkan/vulkan.h>

#include <vkkl/device_memory.h>
#include <vkkl/buffer.h>

#include <vkfu/vulkan/v1.4.328/version_1_0.h>
#include <vkfu/vulkan/v1.4.328/version_1_1.h>

export module demo.renderable.buffer;

import std;
import demo.execution;
import demo.window;
import demo.render;

export struct buffer_type
{
	buffer_type(::VkDevice device, ::VkPhysicalDevice physical_device, ::std::span<::std::byte const> data, ::VkBufferUsageFlags usage);

	[[nodiscard]] ::VkBuffer handle() const noexcept { return _handle.handle; }

	::vkkl::buffer _handle{};
	::vkkl::device_memory _memory{};
};

module : private;

buffer_type::buffer_type(::VkDevice device, ::VkPhysicalDevice physical_device, ::std::span<::std::byte const> data, ::VkBufferUsageFlags usage)
	: _handle(device, ::vkfu::create_buffer(device, ::vkfu::param::buffer{ .size = data.size(), .usage = usage, }))
	, _memory([&]
		{
			assert(!data.empty());
			auto const requirements = ::vkfu::get_buffer_memory_requirements2(device, ::vkfu::param::buffer_memory_requirements2{ .buffer = _handle.handle }).head().memoryRequirements;
			auto const properties = ::vkfu::get_physical_device_memory_properties2(physical_device).head().memoryProperties;
			auto index = ::std::optional<::std::uint32_t>{};
			for (auto i = ::std::uint32_t{}; i < properties.memoryTypeCount; ++i)
			{
				constexpr auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
				if ((requirements.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
				{
					index = i;
					break;
				}
			}
			if (!index)
				throw ::std::runtime_error{ "renderable: no compatible buffer memory type" };
			auto result = ::vkkl::device_memory{ device, ::vkfu::allocate_memory(device, ::vkfu::param::memory{.allocation_size = requirements.size, .type_index = *index }) };
			::vkfu::bind_buffer_memory2(device, ::std::array{ ::vkfu::evaluate(::vkfu::param::bind_buffer_memory{.buffer = _handle.handle, .memory = _memory.handle }) });
			auto mapped = static_cast<void*>(nullptr);
			if (::vkMapMemory(device, _memory.handle, 0, data.size(), 0, &mapped) != VK_SUCCESS)
			{
				throw ::std::runtime_error{ "renderable: map buffer" };
			}
			::std::memcpy(mapped, data.data(), data.size());
			::vkUnmapMemory(device, _memory.handle);
			return result;
		}())
{
};

