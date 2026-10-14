#pragma once

#include <cstdint>

#include <vkkl/vkkl.h>

#include <bvn/assets/sprite_clip.h>

/// Owns one 2D array layer per animation frame; uploaded synchronously at construction.
struct sprite_texture_type
{
	::bvn::assets::sprite_clip_data source;
	::vkkl::device_memory memory;
	::vkkl::image image;
	::vkkl::image_view view;
	::vkkl::sampler sampler;

	/// @pre The caller serializes access to graphics_queue during construction.
	sprite_texture_type(::bvn::assets::sprite_clip_data clip, VkDevice device, VkPhysicalDevice physical_device, VkQueue graphics_queue, ::std::uint32_t graphics_queue_family);
};
