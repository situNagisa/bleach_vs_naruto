#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <ranges>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include <nagisa/concurrency/when_all_range.h>

#include "../framework/shared_state.h"

#include "./input.h"
#include "./render.h"
#include "./fighting_scene/player_sdk.h"
#include "./fighting_scene/player_library.h"

/// 一帧（全部静态图）：
///
/// ```
///             ┌─ renderer.begin ─────────────────────┐
/// when_all ───┤                                      ├─→ record 阶段 ─→ renderer.end
///             └─ poll → players 阶段 → settle ───────┘
/// ```
///
/// player 跟 main_menu 同级：自己持有绘制资源、直接接 renderer、拥有全局资源视角。
/// 嵌在 fighting_scene 里只是为了模块化——场景替它们管加载 / 卸载 / 身份分配，
/// 自己不碰 player 的模拟和绘制。
///
/// - **players 阶段** = `when_all_range`：每个 player 一个 sender，最外层是 `publish`。
///   谁都可以 `find_shared` 谁，但彼此没有 sender 依赖。
/// - **record 阶段** = 同样形状的 `when_all_range`：每个 player 录自己的 secondary，
///   写进 `command_list` 里自己的槽。场景只提供槽位存储。
/// - 模拟和 `renderer.begin`（acquire + 开主命令缓冲）并发。
///
/// 登记早于访问靠 connect 的时机：`when_all_range` 在构造时 connect 全部孩子，之后才 start。
/// **所以 players 阶段内不能有运行时才 connect 的适配器**（`let_value` / `starts_on`），
/// scheduler 在阶段外面就取好、擦除好。
///
/// 这里的 player 全部从 dll 加载。静态链接的 player 也可以并进来：不擦除，跟这个 range
/// 一起放进 `when_all` 即可，形状一样。
struct fighting_scene
{
	struct state
	{
		bool back_requested = false;
	};

	/// 一个 player。成员按声明逆序析构：player 先析构，library 后析构，
	/// 于是 player 的虚析构函数跑完之前 dll 不会被卸掉。
	struct slot
	{
		::std::shared_ptr<player_library> library;
		::std::unique_ptr<dynamic_player> player;
	};

	fighting_scene(input& i, renderer& r);

	[[nodiscard]] auto frame(input& controls, renderer& draw)
	{
		return ::stdexec::read_env(::stdexec::get_scheduler)
			| ::stdexec::let_value([this, &controls, &draw](auto scheduler)
				{
					auto const erased = player_scheduler{ scheduler };
					return draw.frame(_simulate(controls, erased), [this, erased](frame_slot& slot)
						{
							return _record(slot, erased);
						});
				});
	}

	[[nodiscard]] auto _simulate(input& controls, player_scheduler erased)
	{
		return controls.poll()   // 必须在主线程：sync_wait 的线程上内联开始
			| ::stdexec::let_value([this, erased](input::state const& current, input::delta const& changes)
				{
					_registry.reset();   // 在 when_all_range connect 之前：上一帧登记的指针早已悬空
					auto const in = frame_input{ &current, &changes };

					auto players = ::nagisa::concurrency::when_all_range(_players
						| ::std::views::transform([in](slot& entry) { return entry.player->frame(in); }));

					return ::stdexec::write_env(::std::move(players), ::stdexec::env{
							::stdexec::prop{ get_shared_registry, &_registry },
							::stdexec::prop{ ::stdexec::get_scheduler, erased },
						})
						| ::stdexec::then([this, &current, &changes]
							{
								for (auto&& entry : _players)
									entry.player->settle(current);
								_state.back_requested |= changes.keyboard.key(input::key_code::escape).pressed;
							});
				});
	}

	/// sender<command_list>。command_list 只是对场景存储的视图，按值交给 renderer。
	[[nodiscard]] auto _record(frame_slot& target, player_scheduler erased)
	{
		_commands.reset();
		auto players = ::nagisa::concurrency::when_all_range(_players
			| ::std::views::transform([this, &target](slot& entry) { return entry.player->record(target, _commands); }));

		return ::stdexec::write_env(::std::move(players), ::stdexec::prop{ ::stdexec::get_scheduler, erased })
			| ::stdexec::then([this]
				{
					_commands.compact();
					return _commands;
				});
	}

	/// 帧内任何地方都可以提请求；真正生效在 `apply_structural_changes`。
	/// 返回将要分配的 instance，调用方可以立刻拿它当别人的 opponent。
	auto request_spawn(::std::filesystem::path library, ::std::uint32_t opponent) -> ::std::uint32_t
	{
		auto const instance = _next_instance++;
		_pending_spawns.push_back({ ::std::move(library), instance, opponent });
		return instance;
	}

	/// 帧边界调用：`sync_wait` 返回之后，没有任何 operation_state 活着。
	void apply_structural_changes();

	[[nodiscard]] auto&& current_state() const noexcept { return _state; }

	auto _load(::std::filesystem::path const& path) -> ::std::shared_ptr<player_library>;

	state _state{};
	renderer* _renderer;

	// 同一个 dll 只加载一次，多个 player 共享；最后一个 shared_ptr 放手即卸载。
	::std::vector<::std::pair<::std::filesystem::path, ::std::weak_ptr<player_library>>> _libraries;
	::std::vector<slot> _players;
	::std::uint32_t _next_instance = 0;

	struct pending_spawn
	{
		::std::filesystem::path library;
		::std::uint32_t instance;
		::std::uint32_t opponent;
	};
	::std::vector<pending_spawn> _pending_spawns;

	// 按 instance 下标；只在帧边界扩容。
	::std::vector<span_registry::entry> _registry_storage;
	span_registry _registry;
	::std::vector<::VkCommandBuffer> _command_storage;
	command_list _commands;
};
