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

	struct task
	{
		using update_sender_type = decltype(::exec::split(::std::declval<values_sender_type<state, delta>>()));

		struct impl;
		::std::unique_ptr<impl> _impl;

		[[nodiscard]] auto update() const noexcept -> update_sender_type;

		explicit task(setting_scene& scene, input::task& controls, renderer& r, renderer::task& draw);
		task(task&&) noexcept;
		auto operator=(task&&) noexcept -> task&;
		~task();
	};

	explicit setting_scene(renderer& r);
	~setting_scene();

	void build_task(frame_context& context, task& t, renderer& r, renderer::task& rt);
	[[nodiscard]] auto current_state() const noexcept -> state const&;

	game_settings const& preferences() const noexcept;

	struct implementation;
	::std::unique_ptr<implementation> _impl;
};
