#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>

#include <stdexec/execution.hpp>

#include "../app_state.h"
#include "./frame_clock.h"
#include "./input.h"
#include "./render.h"
#include "./detail/immovable.h"
#include "./recording.h"

#include "./setting_scene/settings.h"

inline static auto const setting_path = ::std::filesystem::path(".") / "settings.ini";

/// 设置场景：音量滑块、全屏开关、帧率上限、恢复默认、返回。
///
/// 偏好本身归 main（`app_state::settings_type::preferences`），这里只按输入改它；
/// 返回时先淡出，淡出走完才置 `back_requested`，main 据此保存并切回菜单。
/// 键盘：↑↓ / W S 选行，←→ / A D / 滚轮调值，Enter 切换 / 确认，Esc 返回。
/// 鼠标：悬停选行，点滑块定音量，点开关 / 选项切换。
struct setting_scene : immovable
{
	static constexpr ::std::size_t row_count = 5;

	/// 场景私有的界面快照，`record` 只看它。
	struct state
	{
		::std::size_t selected = 0;
		::std::optional<::std::size_t> hovered{};
		float cursor = 0.0f;                      ///< 高亮所在行（连续值，向 selected 缓动）
		float overlay = 1.0f;                     ///< 顶层黑色遮罩的透明度
		float seconds = 0.0f;
		float volume_knob = 0.0f;                 ///< 滑块位置 0..1，向真实音量缓动
		float toggle_knob = 0.0f;                 ///< 全屏开关位置 0..1
		::std::array<float, row_count> flash{};   ///< 某行的值刚变过的闪光，逐帧衰减
		game_settings shown{};                    ///< 本帧要画的值（偏好的副本）
		bool dirty = false;                       ///< 和进入本场景时不同
	};

	struct delta
	{
		bool moved = false;
		bool changed = false;         ///< 本帧改了某项偏好
		bool back_requested = false;  ///< 淡出完成、本帧交给 main
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
