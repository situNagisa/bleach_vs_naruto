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

#include "../app_state.h"
#include "./frame_clock.h"
#include "./input.h"
#include "./render.h"
#include "./fighting_scene/entity.h"
#include "./fighting_scene/entity_registry.h"
#include "./fighting_scene/module.h"

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

	fighting_scene(input& controls, renderer& draw, module_cache& modules, any_scheduler_type workers);

	[[nodiscard]] values_sender_type<state> update(app_state::fighting_type& shared, frame_clock::frame_time const& time, input::state const& current, input::delta const& changes);
	[[nodiscard]] values_sender_type<recording> record(frame_slot& slot, state const& snapshot);

	/// 帧内任何地方都可以提请求，返回将分配的 instance（可以立刻拿去当别人的引用）。
	auto request_spawn(::std::filesystem::path module, ::std::string kind) -> ::std::uint32_t;
	/// 静态链接的 entity：不经过模块。
	auto request_spawn(::std::unique_ptr<fighting_entity> entity) -> ::std::uint32_t;
	void request_despawn(::std::uint32_t instance);

	/// 帧末由 main 调用：fence 已等过，本帧的并发阶段全部结束。
	void apply_structural_changes();

	struct slot
	{
		::std::uint32_t instance;
		// 声明顺序：module 先声明后析构，entity 的析构函数（代码在 dll 里）跑完之前模块还在。
		::std::shared_ptr<loaded_module> module;
		::std::unique_ptr<fighting_entity> entity;
	};

	struct pending_spawn
	{
		::std::uint32_t instance;
		::std::filesystem::path module;
		::std::string kind;
		::std::unique_ptr<fighting_entity> entity;   ///< 静态 entity 直接给；否则为空，从模块创建
	};

	input& _controls;
	renderer& _draw;
	module_cache& _modules;
	any_scheduler_type _workers;

	::std::vector<slot> _entities{};   ///< 按 instance 升序
	::std::uint32_t _next_instance = 0;
	::std::vector<pending_spawn> _pending_spawns{};
	::std::vector<::std::uint32_t> _pending_despawns{};

	// 每一步的参与者，结构变化时重建：不参与的 entity 不会被连进那一步。
	::std::vector<fighting_entity*> _updating{};
	::std::vector<fighting_entity*> _settling{};
	::std::vector<fighting_entity*> _recording{};
	::std::vector<::VkCommandBuffer> _commands{};   ///< 跟 _recording 一一对应

	entity_registry _registry{};
	frame_view _frame{};
};
