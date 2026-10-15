#pragma once

#include <vkkl/vkkl.h>

/// Sprite-only graphics state: sampler2DArray and a fragment-stage frame push constant.
struct sprite_graphic_pipeline_type
{
	::vkkl::descriptor_set_layout descriptor_layout;
	::vkkl::pipeline_layout layout;
	::vkkl::pipeline pipeline;
	::vkkl::descriptor_pool descriptor_pool;
	::vkkl::descriptor_set descriptor;

	sprite_graphic_pipeline_type(VkDevice device, VkFormat format, VkSampler sampler, VkImageView view);
};
