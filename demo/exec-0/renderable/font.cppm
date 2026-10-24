module;

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <bvn/assets/sprite_clip.h>

export module demo.renderable.font;

import std;
export import demo.renderable.buffer_type;

export
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
