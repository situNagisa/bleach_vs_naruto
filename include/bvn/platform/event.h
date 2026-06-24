#pragma once

#include <functional>

#include <SDL3/SDL_events.h>

#include <bvn/platform/window.h>

namespace bvn::platform
{
/**
 * Per-frame platform events consumed by the main loop.
 */
struct event_state
{
public:
	bool quit_requested = false;
	bool resized = false;
	window_extent drawable_extent;
};

/**
 * Poll all pending SDL events relevant to the target window.
 */
auto poll_events(window const& target) -> event_state;
auto poll_events(window const& target, ::std::function<void(SDL_Event const&)> const& handle_event) -> event_state;
}
