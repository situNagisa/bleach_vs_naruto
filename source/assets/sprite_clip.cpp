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
	namespace
	{
		auto read_file_bytes(std::filesystem::path const& path) -> std::vector<std::byte>
		{
			auto file = std::ifstream{path, std::ios::binary | std::ios::ate};

			if (!file)
			{
				throw std::runtime_error{"failed to open asset: " + path.string()};
			}

			auto size = file.tellg();

			if (size < 0)
			{
				throw std::runtime_error{"failed to size asset: " + path.string()};
			}

			auto bytes = std::vector<std::byte>(static_cast<std::size_t>(size));
			file.seekg(0, std::ios::beg);

			if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), size))
			{
				throw std::runtime_error{"failed to read asset: " + path.string()};
			}

			return bytes;
		}

		auto extension_is(std::filesystem::path const& path, char const* expected) -> bool
		{
			auto extension = path.extension().string();
			std::ranges::transform(extension, extension.begin(), [](unsigned char value)
			{
				return static_cast<char>(std::tolower(value));
			});

			return extension == expected;
		}

		auto to_int_size(std::size_t size) -> int
		{
			if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			{
				throw std::runtime_error{"asset is too large for stb"};
			}

			return static_cast<int>(size);
		}

		auto fallback_delay(int delay) noexcept -> std::chrono::milliseconds
		{
			if (delay <= 0)
			{
				return std::chrono::milliseconds{100};
			}

			return std::chrono::milliseconds{delay};
		}
	}

	auto sprite_clip_data::frame_count() const noexcept -> std::size_t
	{
		return frames.size();
	}

	auto load_sprite_clip(std::filesystem::path const& path) -> sprite_clip_data
	{
		auto bytes = read_file_bytes(path);

		if (extension_is(path, ".gif"))
		{
			auto delays = static_cast<int*>(nullptr);
			auto width = int{};
			auto height = int{};
			auto frame_count = int{};
			auto components = int{};
			auto pixels = stbi_load_gif_from_memory(reinterpret_cast<stbi_uc const*>(bytes.data()), to_int_size(bytes.size()), &delays, &width, &height, &frame_count, &components, STBI_rgb_alpha);

			if (pixels == nullptr)
			{
				throw std::runtime_error{"failed to decode GIF: " + path.string() + ": " + stbi_failure_reason()};
			}

			auto result = sprite_clip_data{};
			result.frame_width = static_cast<std::uint32_t>(width);
			result.frame_height = static_cast<std::uint32_t>(height);
			result.frames_rgba8.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u * static_cast<std::size_t>(frame_count));
			std::ranges::copy_n(reinterpret_cast<std::byte const*>(pixels), result.frames_rgba8.size(), result.frames_rgba8.begin());

			result.frames.reserve(static_cast<std::size_t>(frame_count));

			for (auto index = 0; index < frame_count; ++index)
			{
				auto delay = delays == nullptr ? 100 : delays[index];
				result.frames.push_back(sprite_frame{.delay = fallback_delay(delay)});
			}

			STBI_FREE(delays);
			stbi_image_free(pixels);

			return result;
		}

		auto width = int{};
		auto height = int{};
		auto components = int{};
		auto pixels = stbi_load_from_memory(reinterpret_cast<stbi_uc const*>(bytes.data()), to_int_size(bytes.size()), &width, &height, &components, STBI_rgb_alpha);

		if (pixels == nullptr)
		{
			throw std::runtime_error{"failed to decode image: " + path.string() + ": " + stbi_failure_reason()};
		}

		auto result = sprite_clip_data{};
		result.frame_width = static_cast<std::uint32_t>(width);
		result.frame_height = static_cast<std::uint32_t>(height);
		result.frames_rgba8.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
		std::ranges::copy_n(reinterpret_cast<std::byte const*>(pixels), result.frames_rgba8.size(), result.frames_rgba8.begin());
		result.frames.push_back(sprite_frame{});

		stbi_image_free(pixels);
		return result;
	}

	auto pack_horizontal_atlas(sprite_clip_data const& clip) -> image_rgba8
	{
		if (clip.frame_width == 0 || clip.frame_height == 0 || clip.frames.empty())
		{
			throw std::runtime_error{"cannot pack an empty sprite clip"};
		}

		auto frame_size = static_cast<std::size_t>(clip.frame_width) * static_cast<std::size_t>(clip.frame_height) * 4u;

		if (clip.frames_rgba8.size() != frame_size * clip.frames.size())
		{
			throw std::runtime_error{"sprite clip pixel storage does not match frame metadata"};
		}

		auto atlas = image_rgba8{};
		atlas.width = clip.frame_width * static_cast<std::uint32_t>(clip.frames.size());
		atlas.height = clip.frame_height;
		atlas.pixels.resize(static_cast<std::size_t>(atlas.width) * static_cast<std::size_t>(atlas.height) * 4u);

		for (auto frame_index = std::size_t{}; frame_index < clip.frames.size(); ++frame_index)
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

	watched_sprite_clip::watched_sprite_clip(std::filesystem::path path)
		: watched_path(std::move(path))
	{
		identity = read_identity();
		clip_data = load_sprite_clip(watched_path);
		current_revision = 1;
	}

	auto watched_sprite_clip::path() const noexcept -> std::filesystem::path const&
	{
		return watched_path;
	}

	auto watched_sprite_clip::data() const noexcept -> sprite_clip_data const&
	{
		return clip_data;
	}

	auto watched_sprite_clip::revision() const noexcept -> std::uint64_t
	{
		return current_revision;
	}

	auto watched_sprite_clip::error() const noexcept -> std::string_view
	{
		return last_error;
	}

	auto watched_sprite_clip::poll() noexcept -> bool
	{
		try
		{
			auto next_identity = read_identity();

			if (next_identity.valid == identity.valid && next_identity.file_size == identity.file_size && next_identity.last_write_time == identity.last_write_time)
			{
				return false;
			}

			auto next_data = load_sprite_clip(watched_path);
			clip_data = std::move(next_data);
			identity = next_identity;
			++current_revision;
			last_error.clear();
			return true;
		}
		catch (std::exception const& error)
		{
			last_error = error.what();
			return false;
		}
	}

	auto watched_sprite_clip::read_identity() const -> file_identity
	{
		auto result = file_identity{};

		if (!std::filesystem::exists(watched_path))
		{
			return result;
		}

		result.last_write_time = std::filesystem::last_write_time(watched_path);
		result.file_size = std::filesystem::file_size(watched_path);
		result.valid = true;
		return result;
	}
}
