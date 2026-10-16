#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "../app_state.h"
#include "./frame_clock.h"
#include "./input.h"
#include "./render.h"
#include "./detail/immovable.h"
#include "../framework/frame_context.h"

#include "./setting_scene/settings.h"
#include "./main_menu/buffer_type.h"
#include "./recording.h"

inline static auto const setting_path = ::std::filesystem::path(".") / "settings.ini";

/// 设置场景，持有跨帧状态与绘制资源。
struct setting_scene : immovable
{
	/// 场景私有的界面状态；跨场景的部分（偏好、返回请求）在 main 的 app_state::settings_type 里。
	struct state
	{
		::std::size_t selected = 0;
		::std::optional<::std::size_t> activated{};
	};

	struct delta
	{
		bool moved = false;
		::std::optional<::std::size_t> activated{};
		bool back_requested = false;
	};

	explicit setting_scene(renderer& r);
	~setting_scene();

	/// 按输入修改 main 给的 `shared.preferences`，并写本帧输出 `shared.back_requested`。
	[[nodiscard]] values_sender_type<state, delta> update(app_state::settings_type& shared, frame_clock::frame_time const& time, input::state const& current, input::delta const& changes);

	using recording = secondary_recording;
	/// 完成值持有命令缓冲和顶点缓冲；调用方要让它活到 fence 等完。
	[[nodiscard]] values_sender_type<recording> record(frame_slot&, state const& snapshot);

	struct implementation;
	::std::unique_ptr<implementation> _impl;
};
