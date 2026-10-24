// demo.renderable.buffer_type 的声明，唯一来源。三种展开形态见 execution/execution.h 开头与 README §3。
#if defined(BVN_EXEC0_MODULES) && !defined(BVN_EXEC0_MODULE_INTERFACE)

import std;
import demo.renderable.buffer_type;

#else

#if !defined(BVN_EXEC0_MODULE_INTERFACE)
#include <cstddef>
#include <memory>
#include <span>

#include <vulkan/vulkan.h>
#endif

#ifndef BVN_EXEC0_RENDERABLE_BUFFER_TYPE_H
#define BVN_EXEC0_RENDERABLE_BUFFER_TYPE_H

struct buffer_type
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

#endif

#endif
