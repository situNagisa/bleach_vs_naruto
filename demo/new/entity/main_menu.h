#pragma once

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>
#include <exec/split.hpp>

#include "./input.h"
#include "./render.h"
#include "../framework/frame_context.h"

#include "./detail/immovable.h"

struct frame_context;

/// 主菜单 entity，持有跨帧状态与绘制资源。
struct main_menu : immovable
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

	struct task
	{
		struct impl;
		::std::unique_ptr<impl> _impl;

		[[nodiscard]] auto update() const noexcept;

		explicit task(main_menu& menu, input::task& controls, renderer& r, renderer::task& draw) noexcept;
		task(task&&) noexcept;
		auto operator=(task&&) noexcept -> task&;
		~task();
	};

	main_menu(renderer& r);
	~main_menu();

	void build_task(frame_context& context, task& t, renderer& r, renderer::task& rt);

	state const& current_state() const noexcept;

	struct implementation;
	::std::unique_ptr<implementation> _impl;
};
