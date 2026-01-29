#pragma once

#include <cstddef>

#include "../opengl.h"

namespace aned::asset_system
{
	struct image
	{
		opengl::raii_texture texture{};
		::std::size_t width = 0;
		::std::size_t height = 0;
	};
}

namespace aned::component
{
	using asset_system::image;
}