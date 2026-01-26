#pragma once

#include <stdexcept>
#include <format>
#include <memory>

#include <glad/glad.h>
#include <stb_image.h>


struct image
{
	constexpr static auto texture_deleter = [](GLuint* texture) noexcept { glDeleteTextures(1, texture); };
	::std::unique_ptr<GLuint, decltype(texture_deleter)> texture{};
	int width = 0;
	int height = 0;
};

inline auto load_image(char const* path)
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

	return image{
		.texture{&tex, image::texture_deleter},
		.width = w,
		.height = h,
	};
}



int hitTest(float mx, float my) override {
	float scaled_w = width * scaleX;
	float scaled_h = height * scaleY;
	float x1 = x;
	float y1 = y;
	float x2 = x1 + scaled_w;
	float y2 = y1 + scaled_h;

	if (mx >= x1 && mx < x2 && my >= y1 && my < y2) {
		return id;
	}
	return -1;
}