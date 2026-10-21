#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <stdexec/execution.hpp>

#include <nagisa/concurrency/when_all_range.h>

#include "./fighting_scene.h"

fighting_scene::fighting_scene(input& controls, renderer& draw)
	: _controls(controls)
	, _draw(draw)
{
}

values_sender_type<bool> fighting_scene::update(frame_clock::frame_time const& time, input::state const& current, input::delta const& changes)
{
	return ::stdexec::just() | ::stdexec::let_value([&]
		{
			return changes.keyboard.key(input::key_code::escape).pressed;
		});
}

values_sender_type<::std::vector<::vkkl::command_buffer>, ::std::vector<buffer_type>> fighting_scene::record(frame_slot& slot, state const&) const
{
	return ::stdexec::just() | ::stdexec::let_value([&]
		{
			return ::stdexec::just(::std::vector<::vkkl::command_buffer>{}, ::std::vector<buffer_type>{});
		});
}