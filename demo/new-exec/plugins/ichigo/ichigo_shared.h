#pragma once

#include <atomic>

#include "../../framework/shared_state.h"

#include "../../entity/fighting_scene/player_common.h"

/// ichigo 对外公开的帧内状态。见 naruto_shared.h。
struct ichigo_shared
{
	static constexpr auto shared_type_id = ::stable_hash("bvn.ichigo.shared.v1");

	player_snapshot previous;

	/// 月牙天冲（上一帧）。飞行道具归 ichigo 自己模拟，别人只读它的位置来判定躲没躲开。
	bool getsuga_active = false;
	float getsuga_x = 0.0f;

	fixed_inbox<hit_event, 16> inbox{};

	/// 卍解中。本帧意图。
	::std::atomic<bool> bankai{ false };
};
