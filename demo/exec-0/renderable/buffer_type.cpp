#include <cassert>
#include <vkfu/vulkan/v1.4.328/version_1_0.h>
#include <vkfu/vulkan/v1.4.328/version_1_1.h>
#include <vkkl/buffer.h>
#include <vkkl/device_memory.h>

#include "../common/std.h"
#include "buffer_type.h"

namespace
{
void check(::VkResult result, char const* operation)
{
	if (result != VK_SUCCESS)
		throw ::std::runtime_error{ operation };
}
}

struct buffer_type::impl
{
	::vkkl::device_memory memory{};
	::vkkl::buffer handle{};

	impl() noexcept = default;
	impl(impl&&) noexcept = default;
	impl& operator=(impl&&)= delete;
	impl(::VkDevice device, ::VkPhysicalDevice physical_device, ::std::span<::std::byte const> data, ::VkBufferUsageFlags usage)
	{
		assert(!data.empty());
		auto info = ::VkBufferCreateInfo{};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		info.size = data.size();
		info.usage = usage;
		auto raw = ::VkBuffer{};
		::check(::vkCreateBuffer(device, &info, nullptr, &raw), "buffer: create");
		handle = ::vkkl::buffer{ device, raw };
		auto const requirements = ::vkfu::get_buffer_memory_requirements2(device, ::vkfu::param::buffer_memory_requirements2{ .buffer = handle.handle }).head().memoryRequirements;
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
		memory = ::vkkl::device_memory{ device, ::vkfu::allocate_memory(device, ::vkfu::param::memory{ .allocation_size = requirements.size, .type_index = *index }) };
		::vkfu::bind_buffer_memory2(device, ::std::array{ ::vkfu::evaluate(::vkfu::param::bind_buffer_memory{ .buffer = handle.handle, .memory = memory.handle }) });
		auto mapped = static_cast<void*>(nullptr);
		::check(::vkMapMemory(device, memory.handle, 0, data.size(), 0, &mapped), "renderable: map buffer");
		::std::memcpy(mapped, data.data(), data.size());
		::vkUnmapMemory(device, memory.handle);
	}
};

buffer_type::buffer_type() = default;
buffer_type::~buffer_type() = default;
buffer_type::buffer_type(buffer_type&&) noexcept = default;
buffer_type& buffer_type::operator=(buffer_type&&) noexcept = default;
buffer_type::buffer_type(::VkDevice device, ::VkPhysicalDevice physical_device, ::std::span<::std::byte const> data, ::VkBufferUsageFlags usage) : _impl(::std::make_unique<impl>(device, physical_device, data, usage)) {}
::VkBuffer buffer_type::handle() const noexcept { return _impl ? _impl->handle.handle : VK_NULL_HANDLE; }
