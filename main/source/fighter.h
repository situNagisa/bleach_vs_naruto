#pragma once

#include <chrono>

#include <gif_lib.h>


#include <nagisa/concurrency/concurrency.h>
#include <stdexec/execution.hpp>

#include "./io.h"
#include "./graphic.h"
#include "./physical.h"

#include "./sdl_graphic.h"
#include "./sdl_io.h"

struct player
{
	virtual ~player() = default;
	virtual void process_io(io::keyboard& keyboard) {};
	virtual void process_graphic(graphic::renderer& renderer) {};
	virtual void process_time(::std::chrono::milliseconds delta) {};
};

struct test_player : player
{
	int x{}, y{};
	void process_graphic(graphic::renderer& renderer) override
	{
		renderer.draw_rect(x, y, 50, 50);
	}
};

struct a_player : player
{
	struct GifFrame {
		SDL_Texture* tex;
		int delay_ms;
	};

	struct GifPlayer {
		std::vector<GifFrame> frames;
		int current = 0;
		Uint32 last_tick = 0;
	};

	bool load_gif(
		SDL_Renderer* renderer,
		const char* path,
		GifPlayer& out_player
	) {
		int* delays = nullptr;
		int w = 0, h = 0, frames = 0;

		unsigned char* data =
			stbi_load_gif(path, &delays, &w, &h, &frames, 4);

		if (!data || frames <= 0) {
			return false;
		}

		out_player.frames.reserve(frames);

		for (int i = 0; i < frames; ++i) {
			unsigned char* frame_pixels =
				data + i * w * h * 4;

			SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
				frame_pixels,
				w, h,
				32,
				w * 4,
				0x000000ff,
				0x0000ff00,
				0x00ff0000,
				0xff000000
			);

			if (!surf) continue;

			SDL_Texture* tex =
				SDL_CreateTextureFromSurface(renderer, surf);

			SDL_FreeSurface(surf);

			if (!tex) continue;

			GifFrame frame;
			frame.tex = tex;

			// GIF delay 有些是 0，必须兜底
			frame.delay_ms = delays[i] > 0 ? delays[i] : 16;

			out_player.frames.push_back(frame);
		}

		STBI_FREE(data);
		STBI_FREE(delays);

		out_player.current = 0;
		out_player.last_tick = SDL_GetTicks();

		return !out_player.frames.empty();
	}


	void _update_gif(GifPlayer& gif)
	{
		Uint32 now = SDL_GetTicks();
		if (now - gif.last_tick >= gif.frames[gif.current].delay_ms) {
			gif.current = (gif.current + 1) % gif.frames.size();
			gif.last_tick = now;
		}
	}
	void _draw_gif(GifPlayer& gif, SDL_Renderer* r, int x, int y)
	{
		SDL_RenderCopy(r, gif.frames[gif.current].tex, nullptr, nullptr);
	}

	int x{}, y{};
	a_player()
	{
		
	}
	void process_io(io::keyboard& keyboard) override
	{
		if (keyboard.a())
			x -= 1;
		if (keyboard.d())
			x += 1;
		if (keyboard.w())
			y -= 1;
		if (keyboard.s())
			y += 1;
	}
	void process_graphic(graphic::renderer& renderer) override
	{
		renderer.draw_rect(x, y, 50, 50);
	}
};

::nagisa::concurrency::simple_task<void> fighter_scene()
{
	io io_module{
		.default_keyboard = ::default_keyboard,
	};
	auto keyboard = io_module.default_keyboard();

	graphic graphic_module{
		.default_renderer = ::default_renderer,
	};
	auto renderer = graphic_module.default_renderer();
	auto&& sdl = static_cast<sdl_renderer&>(*renderer);

	a_player a{};
	test_player b{};

	auto token = co_await ::stdexec::get_stop_token();
	while (!token.stop_requested())
	{
		SDL_Event e;
		while (SDL_PollEvent(&e));

		a.process_io(*keyboard);
		// b.process_io(*keyboard);

		sdl._canvas.clear();

		a.process_graphic(*renderer);
		b.process_graphic(*renderer);

		sdl._window.present(sdl._canvas);

		SDL_Delay(1);
	}
	co_return;

}