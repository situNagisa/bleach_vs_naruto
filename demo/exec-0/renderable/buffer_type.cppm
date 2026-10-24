module;

#include <vulkan/vulkan.h>

export module demo.renderable.buffer_type;

import std;

export struct buffer_type
{
	buffer_type();
	buffer_type(::VkDevice device, ::VkPhysicalDevice physical_device, ::std::span<::std::byte const> data, ::VkBufferUsageFlags usage);
	~buffer_type();
	buffer_type(buffer_type&&) noexcept;
	buffer_type& operator=(buffer_type&&) noexcept;
	[[nodiscard]] ::VkBuffer handle() const noexcept;
	struct impl;
	::std::unique_ptr<impl> _impl;
};
