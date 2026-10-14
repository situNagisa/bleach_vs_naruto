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

	struct operation_state
	{
		using update_sender_type = decltype(::exec::split(::std::declval<values_sender_type<state, delta>>()));

		struct impl;
		::std::unique_ptr<impl> _impl;

		explicit operation_state(main_menu& menu, input& i, input::operation_state::shared_state controls, renderer& r, renderer::operation_state::shared_state draw) noexcept;
		~operation_state();

		struct shared_state
		{
			::std::reference_wrapper<operation_state> _state;

			update_sender_type update() const noexcept;
		};
		shared_state start(frame_context& context) & noexcept;
	};

	main_menu(renderer& r);
	~main_menu();

	operation_state connect(input& i, input::operation_state::shared_state controls, renderer& r, renderer::operation_state::shared_state draw) { return operation_state(*this, i, controls, r, draw); }

	state const& current_state() const noexcept;

	struct implementation;
	::std::unique_ptr<implementation> _impl;
};
