#pragma once

#include <nagisa/concurrency/concurrency.h>
#include <stdexec/execution.hpp>
#include <ranges>

#include "../io.h"
#include "../graphic.h"
#include "../physical.h"

#include "../sdl_graphic.h"
#include "../sdl_io.h"

#include "./a_player.h"
#include "./scene_context.h"
#include "./physical.h"

#include <print>

auto process_physical(fighter_scene_context& context, ::std::chrono::milliseconds delta_time)
{
	constexpr static auto gravity = 3000.0f;
	constexpr static auto ground_y = 500.0f;

	for (auto&& phys : context.world.view<physical_component>().each() | ::std::views::values)
	{
 		phys.velocity.y += gravity * delta_time.count();
		phys.position.x += phys.velocity.x * delta_time.count();
		phys.position.y += phys.velocity.y * delta_time.count();

		if (phys.position.y > ground_y)
		{
			phys.position.y = ground_y;
			phys.velocity.y = 0;
		}
	}
}

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

	fighter_scene_context context{};
	try
	{
		a_player a{ context, sdl };

	}
	catch (::std::exception& e)
	{
		::std::println("{}", e.what());
	}
	a_player a{ context, sdl };

	auto token = co_await ::stdexec::get_stop_token();

	auto record_time = ::std::chrono::steady_clock::now();
	while (!token.stop_requested())
	{
		SDL_Event e;
		while (SDL_PollEvent(&e));

		a.process_io(*keyboard);

		sdl._canvas.clear();

		auto delta_time = ::std::chrono::duration_cast<::std::chrono::milliseconds>(
			::std::chrono::steady_clock::now() - record_time
		);
		record_time = ::std::chrono::steady_clock::now();
		a.process_time(delta_time);

		a.process_graphic(*renderer);

		::process_physical(context, delta_time);

		sdl._window.present(sdl._canvas);

		SDL_Delay(1);
	}
	co_return;
}