#pragma once

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>
#include <exec/split.hpp>

#include "./detail/immovable.h"
#include "../framework/entities.h"

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

private:
	using _receiver = ::exec::any_receiver<
		::stdexec::completion_signatures<
			::stdexec::set_value_t(state, delta),
			::stdexec::set_error_t(::std::exception_ptr),
			::stdexec::set_stopped_t()>,
		::exec::queries<::stdexec::inplace_stop_token(::stdexec::get_stop_token_t) noexcept>>;
	using _sender = ::exec::any_sender<_receiver>;
	using _update_sender = decltype(::exec::split(::std::declval<_sender>()));

public:
	struct task
	{
		task(task&&) noexcept;
		~task();

		/// 返回同一个 split 的副本，完成值为状态快照与本次变化。
		[[nodiscard]] auto update() const noexcept -> _update_sender;

	private:
		friend struct main_menu;
		struct implementation;
		explicit task(::std::unique_ptr<implementation> impl) noexcept;
		::std::unique_ptr<implementation> _impl;
	};

	main_menu();
	~main_menu();

	auto build_task(frame_context& context, entity_view<frame_context> view, task_builder builder) -> void;

private:
	struct implementation;
	::std::unique_ptr<implementation> _impl;
};
