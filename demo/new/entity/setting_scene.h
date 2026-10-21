#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "./frame_clock.h"
#include "./input.h"
#include "./render.h"
#include "./detail/immovable.h"
#include "../framework/frame_context.h"

#include "./setting_scene/settings.h"
#include "./main_menu/buffer_type.h"

inline static auto const setting_path = ::std::filesystem::path(".") / "settings.ini";

struct setting_scene : immovable
{
	enum class selected_type : ::std::size_t
	{
		none,
		volume,
		fullscreen,
		back,
	};

	struct state
	{
		selected_type selected{};
		bool activated{};
	};

	explicit setting_scene(renderer& r);
	~setting_scene();

	[[nodiscard]] values_sender_type<state> update(frame_clock::frame_time const& time, input::state const& current, input::delta const& changes);
	[[nodiscard]] values_sender_type<::std::vector<::vkkl::command_buffer>, ::std::vector<buffer_type>> record(frame_slot&, state const& snapshot) const;

	struct implementation;
	::std::unique_ptr<implementation> _impl;
};
