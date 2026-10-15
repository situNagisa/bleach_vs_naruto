#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include <stdexec/execution.hpp>

#include "./input.h"
#include "./render.h"
#include "./main_menu/recording.h"

#include "./detail/immovable.h"

struct main_menu
{
	struct state
	{
		::std::size_t selected = 0;
		::std::optional<::std::size_t> activated{};
	};

	struct delta
	{
		bool moved = false;
		::std::optional<::std::size_t> activated{};
	};

	explicit main_menu(renderer& r);
	~main_menu();

	/// 输入 → 菜单状态。sender<state, delta>。
	[[nodiscard]] auto update(input::state const& current, input::delta const& changes)
	{
		auto [snapshot, result] = _update(current, changes);
		return ::stdexec::just(snapshot, result);
	}

	/// 录制一帧。sender<recording>。跑在组合方通过 env 提供的 scheduler 上。
	[[nodiscard]] auto record(frame_slot&, state const& snapshot)
	{
		return ::stdexec::read_env(::stdexec::get_scheduler)
			| ::stdexec::let_value([this, snapshot](auto scheduler)
				{
					return ::stdexec::schedule(scheduler)
						| ::stdexec::then([this, snapshot] { return _record(snapshot); });
				});
	}

	state const& current_state() const noexcept;

	auto _update(input::state const& current, input::delta const& changes) -> ::std::pair<state, delta>;
	auto _record(state const& snapshot) -> recording;

	struct implementation;
	::std::unique_ptr<implementation> _impl;
	renderer* _renderer;
};
