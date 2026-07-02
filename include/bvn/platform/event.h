#pragma once

#include <vector>

#include <SDL3/SDL_events.h>

#include <bvn/platform/input.h>
#include <bvn/platform/window.h>

namespace bvn::platform
{
/**
 * Per-frame platform events consumed by the main loop.
 */
struct event_state
{
	bool quit_requested = false;
	bool resized = false;
	window_extent drawable_extent;
	input_snapshot input;
	::std::vector<SDL_Event> sdl_events;
};

/**
 * Poll all pending SDL events relevant to the target window.
 */
auto poll_events(window const& target) -> event_state;
}
