#pragma once

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>
#include <exec/split.hpp>

#include "./frame_clock.h"
#include "./input.h"
#include "./render.h"
#include "../framework/frame_context.h"

#include "./detail/immovable.h"
#include "./main_menu/buffer_type.h"

struct main_menu
{
	enum class selected_type : ::std::size_t
	{
		none,
		start_game,
		settings,
		exit,
	};
	struct state
	{
		selected_type selected{};
		bool activated{};
	};

	main_menu(renderer& r);
	~main_menu();

	[[nodiscard]] values_sender_type<state> update(frame_clock::frame_time const& time, input::state const& current, input::delta const& changes);
	[[nodiscard]] static values_sender_type<::std::vector<::vkkl::command_buffer>, ::std::vector<buffer_type>> record(frame_slot&, state const& snapshot);

	struct implementation;
	::std::unique_ptr<implementation> _impl;
};
