#pragma once

#include <stdexcept>
#include <format>

#include <stb_image.h>
#include <glad/glad.h>

#include "../opengl.h"

#include "../image/image.h"


namespace aned::loader
{
	inline auto picture(char const* path)
	{
		int n, w, h;
		auto data = ::stbi_load(path, &w, &h, &n, 4);
		if (!data)
			throw ::std::runtime_error(::std::format("stb_image failed to load: {}\n", path));

		GLuint tex = 0;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glBindTexture(GL_TEXTURE_2D, 0);
		::stbi_image_free(data);

		return component::image{
			.texture{tex},
			.width = static_cast<::std::size_t>(w),
			.height = static_cast<::std::size_t>(h),
		};
	}
}