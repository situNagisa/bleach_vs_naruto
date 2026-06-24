#include <stdexcept>
#include <string>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>

#include <bvn/platform/event.h>

namespace
{
auto sdl_error() -> ::std::string
{
	auto const* message = SDL_GetError();
	if (message == nullptr || message[0] == '\0')
	{
		return "unknown SDL error";
	}

	return message;
}

auto is_resize_event(SDL_Event const& event) noexcept -> bool
{
	return event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
}
}

namespace bvn::platform
{
auto poll_events(window const& target) -> event_state
{
	return poll_events(target, {});
}

auto poll_events(window const& target, ::std::function<void(SDL_Event const&)> const& handle_event) -> event_state
{
	auto state = event_state
	{
		.quit_requested = false,
		.resized = false,
		.drawable_extent = target.drawable_extent(),
	};

	auto const target_id = SDL_GetWindowID(target.native());
	if (target_id == 0)
	{
		throw ::std::runtime_error("SDL_GetWindowID failed: " + sdl_error());
	}

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		if (handle_event)
		{
			handle_event(event);
		}

		if (event.type == SDL_EVENT_QUIT)
		{
			state.quit_requested = true;
		}

		if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == target_id)
		{
			state.quit_requested = true;
		}

		if (is_resize_event(event) && event.window.windowID == target_id)
		{
			state.resized = true;
		}
	}

	if (state.resized)
	{
		state.drawable_extent = target.drawable_extent();
	}

	return state;
}
}
