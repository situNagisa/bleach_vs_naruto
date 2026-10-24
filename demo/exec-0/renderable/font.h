// demo.renderable.font 的声明，唯一来源。三种展开形态见 execution/execution.h 开头与 README §3。
#if defined(BVN_EXEC0_MODULES) && !defined(BVN_EXEC0_MODULE_INTERFACE)

import std;
import demo.renderable.buffer_type;
import demo.renderable.font;

#else

#if !defined(BVN_EXEC0_MODULE_INTERFACE)
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include "buffer_type.h"
#endif

#ifndef BVN_EXEC0_RENDERABLE_FONT_H
#define BVN_EXEC0_RENDERABLE_FONT_H

struct font
{
	struct text
	{
		text();
		text(text&&) noexcept;
		text& operator=(text&&) noexcept;
		~text();
		[[nodiscard]] buffer_type draw(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkCommandBuffer command_buffer, ::glm::mat3x3 transform = ::glm::mat3{ 1.0f }, ::glm::vec4 tint = ::glm::vec4{ 1.0f }) const;
		[[nodiscard]] ::VkExtent2D extent() const noexcept;

		struct impl;
		::std::unique_ptr<impl> _impl;
	};

	font(font&&) noexcept;
	font& operator=(font&&) noexcept;
	~font();
	explicit font(::std::filesystem::path const& path);
	[[nodiscard]] text prepare(::std::string_view utf8, ::std::uint32_t pixel_height, ::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format);
	[[nodiscard]] static ::std::u32string decode_utf8(::std::string_view input);
	struct impl;
	::std::unique_ptr<impl> _impl;
};

#endif

#endif
