#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "./input.h"
#include "./render.h"
#include "./detail/immovable.h"
#include "../framework/frame_context.h"

#include "./setting_scene/settings.h"
#include "./main_menu/buffer_type.h"

inline static auto const setting_path = ::std::filesystem::path(".") / "settings.ini";

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

	[[nodiscard]] values_sender_type<state, delta> update(input::state const& current, input::delta const& changes);

	/// sender<recording>。跑在组合方通过 env 提供的 scheduler 上。
	[[nodiscard]] values_sender_type<::std::vector<::VkCommandBuffer>, ::std::vector<buffer_type>> record(frame_slot&, state const& snapshot);

	[[nodiscard]] auto current_state() const noexcept -> state const&;

	game_settings const& preferences() const noexcept;

	struct implementation;
	::std::unique_ptr<implementation> _impl;
};
