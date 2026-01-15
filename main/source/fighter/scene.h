#pragma once

#include <nagisa/concurrency/concurrency.h>
#include <stdexec/execution.hpp>

#include "../io.h"
#include "../graphic.h"
#include "../physical.h"

#include "../sdl_graphic.h"
#include "../sdl_io.h"

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

	auto token = co_await ::stdexec::get_stop_token();

	auto record_time = ::std::chrono::steady_clock::now();
	while (!token.stop_requested())
	{
		SDL_Event e;
		while (SDL_PollEvent(&e));

		a.process_io(*keyboard);

		sdl._canvas.clear();

		a.process_time(
			::std::chrono::duration_cast<::std::chrono::milliseconds>(
				::std::chrono::steady_clock::now() - record_time
			)
		);
		record_time = ::std::chrono::steady_clock::now();

		a.process_graphic(*renderer);

		sdl._window.present(sdl._canvas);

		SDL_Delay(1);
	}
	co_return;
}