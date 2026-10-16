#pragma once

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>
#include <exec/split.hpp>

#include "../app_state.h"
#include "./frame_clock.h"
#include "./input.h"
#include "./render.h"
#include "../framework/frame_context.h"

#include "./detail/immovable.h"
#include "./main_menu/buffer_type.h"
#include "./recording.h"

struct main_menu
{
	struct state
	{
		::std::size_t selected = 0;
		::std::optional<::std::size_t> activated{};
	};

	struct delta
	{
		bool moved = false;
		::std::optional<::std::size_t> activated{};
	};

	main_menu(renderer& r);
	~main_menu();

	/// 改界面状态，并把本帧输出写进 main 给的 `shared`。完成值是给 record 的界面快照。
	[[nodiscard]] values_sender_type<state, delta> update(app_state::menu_type& shared, frame_clock::frame_time const& time, input::state const& current, input::delta const& changes);
	using recording = secondary_recording;
	/// 完成值持有命令缓冲和顶点缓冲；调用方要让它活到 fence 等完。
	[[nodiscard]] values_sender_type<recording> record(frame_slot&, state const& snapshot);

	struct implementation;
	::std::unique_ptr<implementation> _impl;
};
