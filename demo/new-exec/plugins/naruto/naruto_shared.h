#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "../../framework/shared_state.h"

#include "../../entity/fighting_scene/player_common.h"

/// naruto 对外公开的帧内状态。谁想访问 naruto 就 include 这个头——只有这个头，不需要
/// naruto 的实现，也不需要链接 naruto.dll。naruto 这一帧没加载，`find_shared` 就是 nullptr。
struct naruto_shared
{
	static constexpr auto shared_type_id = ::stable_hash("bvn.naruto.shared.v1");

	/// 上一帧结算后的结果。connect 时写入，本帧只读：任何人并发读都没有竞争。
	player_snapshot previous;

	/// 影分身的位置（上一帧）。别人打分身也算打中 naruto——这是 naruto 定的规则，写在它的头里。
	::std::array<float, 2> clones{};
	::std::uint32_t clone_count = 0;

	/// 别人对 naruto 的作用。本帧并发写，阶段结束后由 naruto 自己结算。
	fixed_inbox<hit_event, 16> inbox{};

	/// 本帧意图，不是结果：读到旧值也行，只能拿来做非决定性的反应。
	::std::atomic<bool> charging{ false };
};
