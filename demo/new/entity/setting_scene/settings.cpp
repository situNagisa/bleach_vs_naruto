#include <algorithm>
#include <cassert>
#include <charconv>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include "./settings.h"

auto load_settings(::std::filesystem::path const& path) -> game_settings
{
	auto settings = game_settings{};
	if (!::std::filesystem::exists(path))
		return settings;
	auto file = ::std::ifstream{ path };
	if (!file)
		throw ::std::runtime_error{ "cannot read settings file: " + path.string() };
	for (auto line = ::std::string{}; ::std::getline(file, line);)
	{
		auto const separator = line.find('=');
		if (separator == ::std::string::npos)
			continue;
		auto const key = ::std::string_view{ line }.substr(0, separator);
		auto const value_text = ::std::string_view{ line }.substr(separator + 1);
		auto value = int{};
		auto const result = ::std::from_chars(value_text.data(), value_text.data() + value_text.size(), value);
		if (result.ec != ::std::errc{} || result.ptr != value_text.data() + value_text.size())
			continue;
		if (key == "volume" && value >= 0 && value <= 100)
			settings.volume = value;
		else if (key == "fullscreen" && (value == 0 || value == 1))
			settings.fullscreen = value != 0;
		else if (key == "frame_rate" && ::std::ranges::find(game_settings::frame_rates, value) != game_settings::frame_rates.end())
			settings.frame_rate = value;
	}
	if (file.bad())
		throw ::std::runtime_error{ "failed to read settings file: " + path.string() };
	return settings;
}

void save_settings(::std::filesystem::path const& path, game_settings const& settings)
{
	assert(settings.volume >= 0 && settings.volume <= 100);
	assert(::std::ranges::find(game_settings::frame_rates, settings.frame_rate) != game_settings::frame_rates.end());
	auto temporary = path;
	temporary += ".tmp";
	auto file = ::std::ofstream{};
	file.exceptions(::std::ios::failbit | ::std::ios::badbit);
	file.open(temporary, ::std::ios::trunc);
	file << "volume=" << settings.volume << '\n'
		<< "fullscreen=" << settings.fullscreen << '\n'
		<< "frame_rate=" << settings.frame_rate << '\n';
	file.close();
	// Replace only after the complete configuration has reached the temporary file.
	::std::filesystem::rename(temporary, path);
}
