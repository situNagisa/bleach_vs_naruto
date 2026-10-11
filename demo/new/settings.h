#pragma once

#include <array>
#include <filesystem>

/// 持久化的用户偏好；frame_rate 为 0 时不额外限制帧率。
struct game_settings
{
	inline static constexpr auto frame_rates = ::std::array{ 30, 60, 90, 120, 144, 240, 0 };

	int volume = 100;
	bool fullscreen = false;
	int frame_rate = 60;

	constexpr auto operator==(game_settings const&) const noexcept -> bool = default;
	[[nodiscard]] constexpr auto volume_gain() const noexcept -> float { return volume / 100.0f; }
};

[[nodiscard]] auto settings_file_path() -> ::std::filesystem::path;
[[nodiscard]] auto load_settings(::std::filesystem::path const& path) -> game_settings;
void save_settings(::std::filesystem::path const& path, game_settings const& settings);
