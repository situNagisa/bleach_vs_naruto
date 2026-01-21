#pragma once

#include <memory>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <stdexcept>
#include <stb_image.h>
#include <SDL2/SDL.h>

namespace animation_impl
{
	struct TextureFrame {
		SDL_Texture* texture = nullptr;
		std::chrono::milliseconds delay{0};
		// per-frame pixel offset
		SDL_Point offset{0,0};
	};

	struct Animator {
		std::vector<TextureFrame> frames{};
		std::size_t current = 0;
		std::chrono::milliseconds elapsed{0};

		// non-copyable to avoid double-free of SDL_Texture*
		Animator() = default;
		// Animator(const Animator&) = delete;
		// Animator& operator=(const Animator&) = delete;
		// Animator(Animator&&) = default;
		// Animator& operator=(Animator&&) = default;

		~Animator()
		{
			for (auto &f : frames)
			{
				if (f.texture)
				{
					SDL_DestroyTexture(f.texture);
					f.texture = nullptr;
				}
			}
		}

		void update(std::chrono::milliseconds delta)
		{
			if (frames.empty())
				return;
			if (elapsed > delta)
			{
				elapsed -= delta;
				return;
			}
			do {
				delta -= elapsed;
				current = (current + 1) % frames.size();
				elapsed = frames[current].delay;
			} while (elapsed <= delta);
			elapsed -= delta;
		}

		SDL_Texture* get_current_texture() const
		{
			if (frames.empty())
				return nullptr;
			return frames[current].texture;
		}

		SDL_Point get_current_offset() const
		{
			if (frames.empty())
				return SDL_Point{0,0};
			return frames[current].offset;
		}
	};

	inline void load_gif_to_animator(SDL_Renderer* renderer, const char* path, Animator& out, const std::vector<SDL_Point>& offsets = {})
	{
		auto f = std::fopen(path, "rb");
		if (!f)
			throw std::runtime_error("fail to open file");
		std::fseek(f, 0, SEEK_END);
		auto size = std::ftell(f);
		std::rewind(f);

		std::vector<unsigned char> fileData(size);
		std::fread(fileData.data(), 1, size, f);
		std::fclose(f);

		int* delays = nullptr;
		int width, height, frames;
		unsigned char* pixels = stbi_load_gif_from_memory(
			fileData.data(), (int)size,
			&delays,
			&width, &height,
			&frames,
			nullptr, 4
		);

		if (!pixels)
			throw std::runtime_error("Failed to load gif");

		for (int i = 0; i < frames; ++i)
		{
			SDL_Texture* tex = SDL_CreateTexture(
				renderer,
				SDL_PIXELFORMAT_RGBA32,
				SDL_TEXTUREACCESS_STATIC,
				width, height
			);
			if (!tex)
			{
				stbi_image_free(pixels);
				stbi_image_free(delays);
				throw std::runtime_error("Failed to create SDL texture");
			}

			SDL_UpdateTexture(
				tex,
				nullptr,
				pixels + i * width * height * 4,
				width * 4
			);

			SDL_Point off{0,0};
			if (!offsets.empty() && (std::size_t)i < offsets.size())
				off = offsets[i];
			TextureFrame tf;
			tf.texture = tex;
			tf.delay = std::chrono::milliseconds(delays ? delays[i] : 100);
			tf.offset = off;
			out.frames.push_back(std::move(tf));
		}

		stbi_image_free(pixels);
		stbi_image_free(delays);
	}
}
