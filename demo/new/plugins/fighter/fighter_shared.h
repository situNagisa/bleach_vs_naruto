#pragma once

#include <cstdint>

#include "../../entity/fighting_scene/entity_registry.h"

/// fighter 对外公开的帧内状态。谁想访问 fighter 就 include 这个头——不需要实现，也不需要链接
/// fighter 模块。跨模块时它就是 fighter 的 ABI，所以只放稳定布局的数据。
struct fighter_hit
{
	::std::uint32_t attacker;
	int damage;
	float knockback;
};

struct fighter_shared
{
	static constexpr auto shared_type_id = ::stable_hash("bvn.fighter.shared.v1");

	/// settle 里由拥有者写；update 阶段别人只读。两步之间有屏障，不需要 atomic。
	float x = 0.0f;
	int hp = 100;

	/// update 阶段别人写；settle 阶段拥有者读、清空。
	mutable fixed_inbox<fighter_hit, 16> inbox{};
};
