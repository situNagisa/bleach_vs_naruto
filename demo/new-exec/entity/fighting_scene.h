#pragma once

#include <stdexec/execution.hpp>

#include "./input.h"
#include "./render.h"

struct fighting_scene
{
	struct state
	{
		bool back_requested = false;
	};

	fighting_scene(input& i, renderer& r);

	/// 本场景目前不画东西，所以不走 renderer：一帧就是 poll → update。
	/// 以后要画，换成 `scene_frame(controls, *this, draw)` 并补上 `record`。
	[[nodiscard]] auto frame(input& controls)
	{
		return controls.poll()
			| ::stdexec::then([this](input::state const&, input::delta const& changes)
				{
					_state.back_requested |= changes.keyboard.key(input::key_code::escape).pressed;
				});
	}

	[[nodiscard]] auto&& current_state() const noexcept { return _state; }

	state _state{};
};
