module;

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <bvn/assets/sprite_clip.h>

export module demo.renderable.sprite;

import std;
export import demo.renderable.buffer_type;

export
struct sprite
{
	

	sprite(sprite&&) noexcept;
	sprite& operator=(sprite&&) noexcept;
	~sprite();

	sprite(::bvn::assets::sprite_clip_data data, ::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format);

	[[nodiscard]] buffer_type draw(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkCommandBuffer command_buffer, ::std::size_t frame, ::glm::mat3x3 transform = ::glm::mat3{ 1.0f }) const;
	[[nodiscard]] ::VkExtent2D extent() const noexcept;
	[[nodiscard]] ::std::size_t frame_count() const noexcept;

	struct impl;
	::std::unique_ptr<impl> _impl;
};
