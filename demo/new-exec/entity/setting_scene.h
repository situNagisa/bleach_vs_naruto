#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

#include <stdexec/execution.hpp>

#include "./input.h"
#include "./render.h"
#include "./main_menu/recording.h"
#include "./detail/immovable.h"

#include "./setting_scene/settings.h"

inline static auto setting_path = ::std::filesystem::path(".") / "settings.ini";

/// 设置场景，持有跨帧状态与绘制资源。
struct setting_scene : immovable
{
	struct state
	{
		::std::size_t selected = 0;
		::std::optional<::std::size_t> activated{};
		bool back_requested = false;
	};

	struct delta
	{
		bool moved = false;
		::std::optional<::std::size_t> activated{};
		bool back_requested = false;
	};

	explicit setting_scene(renderer& r);
	~setting_scene();

	/// sender<state, delta>。
	[[nodiscard]] auto update(input::state const& current, input::delta const& changes)
	{
		auto [snapshot, result] = _update(current, changes);
		return ::stdexec::just(snapshot, result);
	}

	/// sender<recording>。跑在组合方通过 env 提供的 scheduler 上。
	[[nodiscard]] auto record(frame_slot&, state const& snapshot)
	{
		return ::stdexec::read_env(::stdexec::get_scheduler)
			| ::stdexec::let_value([this, snapshot](auto scheduler)
				{
					return ::stdexec::schedule(scheduler)
						| ::stdexec::then([this, snapshot] { return _record(snapshot); });
				});
	}

	[[nodiscard]] auto current_state() const noexcept -> state const&;

	game_settings const& preferences() const noexcept;

	auto _update(input::state const& current, input::delta const& changes) -> ::std::pair<state, delta>;
	auto _record(state const& snapshot) -> recording;

	struct implementation;
	::std::unique_ptr<implementation> _impl;
	renderer* _renderer;
};
