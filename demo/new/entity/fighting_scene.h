#pragma once

#include <utility>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "../framework/any_sender.h"
#include "../framework/frame_context.h"

#include "./input.h"
#include "./render.h"

struct fighting_scene
{
	struct state
	{
		bool back_requested = false;
	};

	struct operation_state
	{
		using update_sender_type = decltype(::exec::split(::std::declval<values_sender_type<state>>()));

		update_sender_type _update;

		explicit operation_state(fighting_scene& scene, input& i, input::operation_state::shared_state controls, renderer& r, renderer::operation_state::shared_state draw);
		[[nodiscard]] auto update() const noexcept { return _update; }

		struct shared_state
		{
			::std::reference_wrapper<operation_state> _state;
			[[nodiscard]] auto update() const noexcept { return _state.get()._update; }
		};
		shared_state start(frame_context& context) & noexcept;
	};

	operation_state connect(input& i, input::operation_state::shared_state controls, renderer& r, renderer::operation_state::shared_state draw)
	{
		return operation_state(*this, i, controls, r, draw);
	}

	state _state{};

	fighting_scene(input& i, renderer& r);
	[[nodiscard]] auto&& current_state() const noexcept { return _state; }
};
