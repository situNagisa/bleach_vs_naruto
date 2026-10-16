#pragma once

#include <cstddef>
#include <optional>

#include "./entity/setting_scene/settings.h"

/// main 规定的跨场景状态。main 持有它，每帧把对应那一部分的引用交给场景的 `update`，场景按约定修改；
/// main 在帧结束后读，决定状态机怎么走。长什么样由 main 定，场景只认自己那一块。
///
/// 场景自己的界面状态（选中项、按键重复计时）不在这里，仍归场景私有。
///
/// 字段分两类：
/// - 持久：跨帧保留，场景读改（settings.preferences）。
/// - 本帧输出：场景每次 `update` 都整体覆盖，不残留到下一帧（activated / back_requested）。
struct app_state
{
	struct menu_type
	{
		::std::optional<::std::size_t> activated{};   ///< 本帧输出：本帧确认的选项
	};

	struct settings_type
	{
		game_settings preferences{};                  ///< 持久：由 main 加载、保存
		bool back_requested = false;                  ///< 本帧输出
	};

	struct fighting_type
	{
		bool back_requested = false;                  ///< 本帧输出
	};

	menu_type menu{};
	settings_type settings{};
	fighting_type fighting{};
};
