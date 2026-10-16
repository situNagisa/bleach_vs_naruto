#pragma once

#include <utility>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "../framework/any_sender.h"
#include "../framework/frame_context.h"

#include "./main_menu/buffer_type.h"

#include "./input.h"
#include "./render.h"

struct fighting_scene
{
	struct state
	{
		bool back_requested = false;
	};

	[[nodiscard]] values_sender_type<state> update(input::state const& current, input::delta const& changes);
	[[nodiscard]] values_sender_type<::std::vector<::VkCommandBuffer>, ::std::vector<buffer_type>> record(frame_slot&, state const& snapshot);

	fighting_scene(input& i, renderer& r);
};
