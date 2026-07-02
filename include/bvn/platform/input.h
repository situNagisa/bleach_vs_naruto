#pragma once

#include <array>
#include <cstddef>

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <bvn/platform/window.h>

namespace bvn::platform
{
	/**
	 * Per-frame snapshot of held keys + accumulated relative mouse motion.
	 *
	 * Filled by poll_events: `keys` mirrors SDL's keyboard state at poll time and
	 * `mouse_dx/mouse_dy` sum the relative motion reported since the previous poll
	 * (meaningful while relative mouse mode is enabled, for mouse-look).
	 */
	struct input_snapshot
		{
			::std::array<bool, SDL_SCANCODE_COUNT> keys = {};
			float mouse_dx = 0.0f;
			float mouse_dy = 0.0f;
		};

	/**
	 * Enable/disable relative mouse mode (cursor hidden + captured) on the window,
	 * used to drive a free-look camera.
	 */
	void relative_mouse_mode(window const& target, bool enabled) noexcept;
}
