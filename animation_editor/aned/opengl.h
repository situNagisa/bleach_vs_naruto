#pragma once

#include <memory>

#include <glad/glad.h>

namespace aned::opengl
{
	struct raii_texture
	{
		constexpr raii_texture() noexcept = default;
		constexpr raii_texture(GLuint tex) noexcept : value(tex) {}
		constexpr raii_texture(raii_texture const&) noexcept = delete;
		constexpr raii_texture& operator=(raii_texture const&) noexcept = delete;
		constexpr raii_texture(raii_texture&& other) noexcept : value(::std::exchange(other.value, 0)) {}
		constexpr raii_texture& operator=(raii_texture&& other) noexcept
		{
			if (this != &other)
			{
				auto _ = ::std::move(*this);
				value = ::std::exchange(other.value, 0);
			}
			return *this;
		}
		~raii_texture()
		{
			glDeleteTextures(1, &value);
		}

		GLuint value{};
	};
}