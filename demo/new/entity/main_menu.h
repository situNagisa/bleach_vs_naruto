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
#include "./main_menu/buffer_type.h"

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

	main_menu(renderer& r);
	~main_menu();

	[[nodiscard]] values_sender_type<state, delta> update(input::state const& current, input::delta const& changes);
	[[nodiscard]] values_sender_type<::std::vector<::VkCommandBuffer>, ::std::vector<buffer_type>> record(frame_slot&, state const& snapshot);

	state const& current_state() const noexcept;

	struct implementation;
	::std::unique_ptr<implementation> _impl;
};
