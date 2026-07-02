#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

#include <bvn/platform/event.h>
#include <bvn/platform/input.h>

namespace bvn::platform
{
auto poll_events(window const& target) -> event_state
{
		auto state = event_state
		{
			.quit_requested = false,
			.resized = false,
			.drawable_extent = target.drawable_extent(),
			.input = {},
			.sdl_events = {},
		};

	auto const target_id = SDL_GetWindowID(target.handle);
	if (target_id == 0)
	{
		auto const* message = SDL_GetError();
		throw ::std::runtime_error{message == nullptr || message[0] == '\0' ? "SDL_GetWindowID failed: unknown SDL error" : "SDL_GetWindowID failed: " + ::std::string{message}};
	}

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		state.sdl_events.push_back(event);

		if (event.type == SDL_EVENT_QUIT)
		{
			state.quit_requested = true;
		}

		if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == target_id)
		{
			state.quit_requested = true;
		}

		if ((event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) && event.window.windowID == target_id)
		{
			state.resized = true;
		}

		if (event.type == SDL_EVENT_MOUSE_MOTION)
		{
			state.input.mouse_dx += event.motion.xrel;
			state.input.mouse_dy += event.motion.yrel;
		}
	}

	auto key_count = int{};
	auto const* keyboard = SDL_GetKeyboardState(&key_count);
	if (keyboard != nullptr)
	{
		auto const count = ::std::min(static_cast<::std::size_t>(key_count), state.input.keys.size());
		for (auto index = ::std::size_t{}; index < count; ++index)
		{
			state.input.keys[index] = keyboard[index];
		}
	}

	if (state.resized)
	{
		state.drawable_extent = target.drawable_extent();
	}

	return state;
}

void relative_mouse_mode(window const& target, bool enabled) noexcept
{
	SDL_SetWindowRelativeMouseMode(target.handle, enabled);
}
}
