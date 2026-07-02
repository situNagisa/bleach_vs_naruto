#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace bvn::assets
{
	struct image_rgba8
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::vector<std::byte> pixels;
	};

	struct sprite_clip_data
	{
		std::uint32_t frame_width = 0;
		std::uint32_t frame_height = 0;
		std::size_t frame_count = 0;
		std::vector<std::byte> frames_rgba8;
	};

	auto load_sprite_clip(std::filesystem::path const& path) -> sprite_clip_data;
	auto pack_horizontal_atlas(sprite_clip_data const& clip) -> image_rgba8;
}
