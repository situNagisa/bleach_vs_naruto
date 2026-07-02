#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <ranges>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <bvn/assets/sprite_clip.h>

namespace bvn::assets
{
	auto load_sprite_clip(std::filesystem::path const& path) -> sprite_clip_data
	{
		auto bytes = ::std::vector<::std::byte>{};

		//+ read asset bytes
		{
			auto file = ::std::ifstream{path, ::std::ios::binary | ::std::ios::ate};

			if (!file)
			{
				throw ::std::runtime_error{"failed to open asset: " + path.string()};
			}

			auto size = file.tellg();

			if (size < 0)
			{
				throw ::std::runtime_error{"failed to size asset: " + path.string()};
			}

			bytes.resize(static_cast<::std::size_t>(size));
			file.seekg(0, ::std::ios::beg);

			if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), size))
			{
				throw ::std::runtime_error{"failed to read asset: " + path.string()};
			}
		}

		if (bytes.size() > static_cast<::std::size_t>(::std::numeric_limits<int>::max()))
		{
			throw ::std::runtime_error{"asset is too large for stb"};
		}

		auto extension = path.extension().string();
		::std::ranges::transform(extension, extension.begin(), [](unsigned char value)
		{
			return static_cast<char>(::std::tolower(value));
		});

		if (extension == ".gif")
		{
			auto delays = static_cast<int*>(nullptr);
			auto width = int{};
			auto height = int{};
			auto frame_count = int{};
			auto components = int{};
			auto pixels = stbi_load_gif_from_memory(reinterpret_cast<stbi_uc const*>(bytes.data()), static_cast<int>(bytes.size()), &delays, &width, &height, &frame_count, &components, STBI_rgb_alpha);

			if (pixels == nullptr)
			{
				throw ::std::runtime_error{"failed to decode GIF: " + path.string() + ": " + stbi_failure_reason()};
			}

			auto result = sprite_clip_data{};
			result.frame_width = static_cast<std::uint32_t>(width);
			result.frame_height = static_cast<std::uint32_t>(height);
			result.frame_count = static_cast<::std::size_t>(frame_count);
			result.frames_rgba8.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u * static_cast<std::size_t>(frame_count));
			std::ranges::copy_n(reinterpret_cast<std::byte const*>(pixels), result.frames_rgba8.size(), result.frames_rgba8.begin());

			STBI_FREE(delays);
			stbi_image_free(pixels);

			return result;
		}

		auto width = int{};
		auto height = int{};
		auto components = int{};
		auto pixels = stbi_load_from_memory(reinterpret_cast<stbi_uc const*>(bytes.data()), static_cast<int>(bytes.size()), &width, &height, &components, STBI_rgb_alpha);

		if (pixels == nullptr)
		{
			throw ::std::runtime_error{"failed to decode image: " + path.string() + ": " + stbi_failure_reason()};
		}

		auto result = sprite_clip_data{};
		result.frame_width = static_cast<std::uint32_t>(width);
		result.frame_height = static_cast<std::uint32_t>(height);
		result.frame_count = 1;
		result.frames_rgba8.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
		std::ranges::copy_n(reinterpret_cast<std::byte const*>(pixels), result.frames_rgba8.size(), result.frames_rgba8.begin());

		stbi_image_free(pixels);
		return result;
	}

	auto pack_horizontal_atlas(sprite_clip_data const& clip) -> image_rgba8
	{
		if (clip.frame_width == 0 || clip.frame_height == 0 || clip.frame_count == 0)
		{
			throw ::std::runtime_error{"cannot pack an empty sprite clip"};
		}

		auto frame_size = static_cast<std::size_t>(clip.frame_width) * static_cast<std::size_t>(clip.frame_height) * 4u;

		if (clip.frames_rgba8.size() != frame_size * clip.frame_count)
		{
			throw ::std::runtime_error{"sprite clip pixel storage does not match frame metadata"};
		}

		auto atlas = image_rgba8{};
		atlas.width = clip.frame_width * static_cast<std::uint32_t>(clip.frame_count);
		atlas.height = clip.frame_height;
		atlas.pixels.resize(static_cast<std::size_t>(atlas.width) * static_cast<std::size_t>(atlas.height) * 4u);

		for (auto frame_index = std::size_t{}; frame_index < clip.frame_count; ++frame_index)
		{
			for (auto y = std::uint32_t{}; y < clip.frame_height; ++y)
			{
				auto source_offset = frame_index * frame_size + static_cast<std::size_t>(y) * clip.frame_width * 4u;
				auto destination_offset = (static_cast<std::size_t>(y) * atlas.width + frame_index * clip.frame_width) * 4u;
				std::ranges::copy_n(clip.frames_rgba8.begin() + static_cast<std::ptrdiff_t>(source_offset), static_cast<std::ptrdiff_t>(clip.frame_width * 4u), atlas.pixels.begin() + static_cast<std::ptrdiff_t>(destination_offset));
			}
		}

		return atlas;
	}
}
