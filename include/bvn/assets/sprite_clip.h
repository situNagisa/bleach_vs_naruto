#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace bvn::assets
{
	struct image_rgba8
	{
	public:
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::vector<std::byte> pixels;
	};

	struct sprite_frame
	{
	public:
		::std::chrono::milliseconds delay{100};
	};

	struct sprite_clip_data
	{
	public:
		std::uint32_t frame_width = 0;
		std::uint32_t frame_height = 0;
		std::vector<std::byte> frames_rgba8;
		std::vector<sprite_frame> frames;

		auto frame_count() const noexcept -> std::size_t;
	};

	auto load_sprite_clip(std::filesystem::path const& path) -> sprite_clip_data;
	auto pack_horizontal_atlas(sprite_clip_data const& clip) -> image_rgba8;

	struct watched_sprite_clip
	{
	public:
		watched_sprite_clip() = default;
		explicit watched_sprite_clip(std::filesystem::path path);

		auto path() const noexcept -> std::filesystem::path const&;
		auto data() const noexcept -> sprite_clip_data const&;
		auto revision() const noexcept -> std::uint64_t;
		auto error() const noexcept -> std::string_view;
		auto poll() noexcept -> bool;

	private:
		struct file_identity
		{
			std::filesystem::file_time_type last_write_time{};
			std::uintmax_t file_size = 0;
			bool valid = false;
		};

		auto read_identity() const -> file_identity;

		std::filesystem::path watched_path;
		sprite_clip_data clip_data;
		std::uint64_t current_revision = 0;
		std::string last_error;
		file_identity identity;
	};
}
