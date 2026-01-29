#pragma once

#include <stdexcept>
#include <format>

#include <stb_image.h>

#include "../asset/image.h"


namespace aned::loader
{
	inline auto picture(char const* path)
	{
		int n, w, h;
		auto data = ::stbi_load(path, &w, &h, &n, 4);
		if (!data)
			throw ::std::runtime_error(::std::format("stb_image failed to load: {}\n", path));

		auto result = component::image::create(reinterpret_cast<::std::byte const*>(data), static_cast<::std::size_t>(w), static_cast<::std::size_t>(h));
		::stbi_image_free(data);
		return result;
	}
}