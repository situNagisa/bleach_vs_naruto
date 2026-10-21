#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include <stdexec/execution.hpp>

#include "../framework/any_sender.h"
#include "../framework/frame_slot.h"

#include "./frame_clock.h"
#include "./input.h"
#include "./render.h"
#include "./fighting_scene/entity.h"
#include "./fighting_scene/entity_registry.h"
#include "./fighting_scene/module.h"
#include "./main_menu/buffer_type.h"

/// 格斗场景：一组动态的 entity（player、物理、特效……），可以从模块里加载。
///
/// 场景自己不模拟、不绘制，只做三件事：
/// 1. 管 entity 的身份和生命期（spawn / despawn 只在帧末生效）；
/// 2. 提供 registry 和命令槽位的存储（只在结构变化时扩容）；
/// 3. 把每一步排成静态图：publish（同步）→ update ∥ → settle ∥ → record ∥。
struct fighting_scene
{
	struct state
	{
	};

	/// 对场景命令槽的视图；命令缓冲本身由各 entity 跨帧持有。
	struct recording
	{
		::std::span<::VkCommandBuffer const> _commands{};

		[[nodiscard]] auto commands() const noexcept -> ::std::span<::VkCommandBuffer const> { return _commands; }
	};

	fighting_scene(input& controls, renderer& draw);

	[[nodiscard]] values_sender_type<bool> update(frame_clock::frame_time const& time, input::state const& current, input::delta const& changes);
	[[nodiscard]] values_sender_type<::std::vector<::vkkl::command_buffer>, ::std::vector<buffer_type>> record(frame_slot& slot, state const& snapshot) const;

	input& _controls;
	renderer& _draw;
};
