#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

#include <stdexec/execution.hpp>

#include "../app_state.h"
#include "./frame_clock.h"
#include "./input.h"
#include "./render.h"

#include "./detail/immovable.h"
#include "./recording.h"

/// 主菜单：START GAME / SETTINGS / EXIT。
///
/// 键盘（↑↓ / W S / 滚轮选，Enter / Space 确认，Esc 跳到 EXIT）和鼠标（悬停选、左键确认）都能操作。
/// 进入时淡入；确认后先淡出再把选项交给 main（`shared.activated`），淡出期间不再响应输入。
struct main_menu
{
	static constexpr ::std::size_t option_count = 3;

	/// 场景私有的界面快照，`record` 只看它。
	struct state
	{
		::std::size_t selected = 0;
		::std::optional<::std::size_t> hovered{};   ///< 鼠标悬停的行
		float cursor = 0.0f;                        ///< 高亮条所在行（连续值，向 selected 缓动）
		float overlay = 1.0f;                       ///< 顶层黑色遮罩的透明度：1 全黑
		float seconds = 0.0f;                       ///< 场景已运行时间，驱动标题脉动 / 背景漂移
		::std::array<float, option_count> press{};  ///< 每行确认时的闪光，逐帧衰减
	};

	struct delta
	{
		bool moved = false;
		::std::optional<::std::size_t> activated{};   ///< 淡出完成、本帧交给 main 的选项
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
