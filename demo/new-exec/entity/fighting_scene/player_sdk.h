#pragma once

#include <cstdint>
#include <exception>
#include <memory>

#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>

#include "../../framework/shared_state.h"

#include "../render.h"
#include "./player_common.h"

/// 格斗场景的 player SDK。宿主和插件共享这份头、同一套工具链，C++ 类型（sender、
/// unique_ptr、虚函数、renderer&）直接过 dll 边界，没有 C ABI 那层。
///
/// 插件作者写的 player 就是一个普通 entity：`frame` 返回具体 sender、
/// 最外层是 `publish`、用 `auto& registry` 查别人；`record` 直接拿 renderer 的设备录 secondary。
/// 写完一行 `BVN_EXPORT_PLAYER(naruto)` 导出。
///
/// 类型擦除只在 `dynamic_player_impl` 这一处：静态 player 不擦除，插件 player 每帧每个阶段
/// 多一次 any_sender 的连接。下面这些具体类型是**这个场景**的约定，不是框架的。

/// 本场景的 registry。插件里 `auto& registry` 推出来的就是它。
using player_registry = span_registry;

using player_completions = ::stdexec::completion_signatures<
	::stdexec::set_value_t(),
	::stdexec::set_error_t(::std::exception_ptr),
	::stdexec::set_stopped_t()>;

using player_schedule_receiver = ::exec::any_receiver<player_completions,
	::exec::queries<::stdexec::inplace_stop_token(::stdexec::get_stop_token_t) noexcept>>;
using player_scheduler = ::exec::any_scheduler<::exec::any_sender<player_schedule_receiver>>;

/// 模拟阶段：env 里有 stop token、scheduler、registry。
using player_frame_receiver = ::exec::any_receiver<player_completions,
	::exec::queries<
		::stdexec::inplace_stop_token(::stdexec::get_stop_token_t) noexcept,
		player_scheduler(::stdexec::get_scheduler_t) noexcept,
		player_registry*(get_shared_registry_t) noexcept>>;
using player_frame_sender = ::exec::any_sender<player_frame_receiver>;

/// 录制阶段：没有 registry（共享状态只活在模拟阶段）。
using player_record_receiver = ::exec::any_receiver<player_completions,
	::exec::queries<
		::stdexec::inplace_stop_token(::stdexec::get_stop_token_t) noexcept,
		player_scheduler(::stdexec::get_scheduler_t) noexcept>>;
using player_record_sender = ::exec::any_sender<player_record_receiver>;

/// 场景 spawn 时给的身份和宿主资源。instance 就是 registry 和 command_list 的下标，生命期内不变。
struct player_spawn
{
	::std::uint32_t instance;
	::std::uint32_t opponent;
	renderer* draw;
};

/// 插件 player 在宿主眼里的样子。形状和静态 player 相同，场景对两者一视同仁。
struct dynamic_player
{
	virtual ~dynamic_player() = default;

	/// **返回的 sender 被 connect 时就完成登记**（any_sender 的 connect 会连内层，内层最外面
	/// 是 publish）。所以放进场景的 when_all_range 里，登记照样早于任何人 start。
	[[nodiscard]] virtual auto frame(frame_input in) -> player_frame_sender = 0;
	virtual void settle(input::state const& current) = 0;
	[[nodiscard]] virtual auto record(frame_slot& slot, command_list& list) -> player_record_sender = 0;
	[[nodiscard]] virtual auto snapshot() const noexcept -> player_snapshot const& = 0;
};

/// 把一个普通 player 包成 dynamic_player。在插件里实例化，宿主不需要认识 `Player`。
template <class Player>
struct dynamic_player_impl final : dynamic_player
{
	Player _player;

	explicit dynamic_player_impl(player_spawn spawn) : _player(spawn.instance, spawn.opponent, *spawn.draw) {}

	auto frame(frame_input in) -> player_frame_sender override { return _player.frame(in); }
	void settle(input::state const& current) override { _player.settle(current); }
	auto record(frame_slot& slot, command_list& list) -> player_record_sender override { return _player.record(slot, list); }
	auto snapshot() const noexcept -> player_snapshot const& override { return _player.snapshot(); }
};

inline constexpr ::std::uint32_t player_sdk_version = 1;

/// 插件导出的唯一符号返回它。只校验"是同一份 SDK 头"，不做 ABI 兼容。
struct player_module
{
	::std::uint32_t sdk_version;
	auto (*create)(player_spawn) -> ::std::unique_ptr<dynamic_player>;
};

#if defined(_WIN32)
#	define BVN_PLAYER_EXPORT __declspec(dllexport)
#else
#	define BVN_PLAYER_EXPORT __attribute__((visibility("default")))
#endif

/// `extern "C"` 只为了符号名不被 mangle、好用名字找；返回的是 C++ 结构。
#define BVN_EXPORT_PLAYER(PlayerType)                                                          \
	extern "C" BVN_PLAYER_EXPORT auto bvn_player_module() noexcept -> ::player_module const*  \
	{                                                                                          \
		static constexpr auto module = ::player_module{                                        \
			.sdk_version = ::player_sdk_version,                                               \
			.create = [](::player_spawn spawn) -> ::std::unique_ptr<::dynamic_player>          \
				{                                                                              \
					return ::std::make_unique<::dynamic_player_impl<PlayerType>>(spawn);       \
				},                                                                             \
		};                                                                                     \
		return &module;                                                                        \
	}
