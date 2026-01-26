#pragma once

#include <memory>

#include <glad/glad.h>

namespace aned::opengl
{
	constexpr static auto texture_deleter = [](GLuint* texture) noexcept { glDeleteTextures(1, texture); };
	using raii_texture = ::std::unique_ptr<GLuint, decltype(texture_deleter)>;
}