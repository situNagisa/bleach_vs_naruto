module;

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <bvn/assets/sprite_clip.h>

export module demo.renderable.image;

import std;
export import demo.renderable.buffer_type;

export
struct image
{
	

	image(image&&) noexcept;
	image& operator=(image&&) noexcept;
	~image();

	image(::bvn::assets::image_rgba8 data, ::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format);

	[[nodiscard]] buffer_type draw(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkCommandBuffer command_buffer, ::glm::mat3x3 transform = ::glm::mat3{ 1.0f }, ::glm::vec4 tint = ::glm::vec4{ 1.0f }) const;
	[[nodiscard]] ::VkExtent2D extent() const noexcept;

	struct impl;
	::std::unique_ptr<impl> _impl;
};
