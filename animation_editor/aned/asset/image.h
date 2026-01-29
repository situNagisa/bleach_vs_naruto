#pragma once

#include <cstddef>

#include "../opengl.h"

namespace aned::asset
{
	struct image
	{
		static auto create(::std::byte const* data, ::std::size_t width, ::std::size_t height)
		{
			GLuint tex = 0;
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glBindTexture(GL_TEXTURE_2D, 0);

			return image{
				.texture{tex},
				.width = width,
				.height = height,
			};
		}

		opengl::raii_texture texture{};
		::std::size_t width = 0;
		::std::size_t height = 0;
	};
}

namespace aned::component
{
	using asset::image;
}