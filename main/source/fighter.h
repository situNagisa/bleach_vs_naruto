#pragma once

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
	virtual void process_io(io::keyboard& keyboard) = 0;
	virtual void process_graphic(graphic::renderer& renderer) = 0;
};

struct test_player : player
{
	int x{}, y{};
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

	test_player a{}, b{};

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