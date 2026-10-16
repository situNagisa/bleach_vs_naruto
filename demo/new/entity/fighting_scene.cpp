#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "./fighting_scene.h"

values_sender_type<fighting_scene::state> fighting_scene::update(input::state const& current, input::delta const& changes)
{
	return ::stdexec::just(state{ .back_requested = changes.keyboard.key(input::key_code::escape).pressed });
}

values_sender_type<::std::vector<VkCommandBuffer>, ::std::vector<buffer_type>> fighting_scene::record(frame_slot&, state const& snapshot)
{
	return ::stdexec::just(::std::vector<VkCommandBuffer>{}, ::std::vector<buffer_type>{});
}

fighting_scene::fighting_scene(input& i, renderer& r)
{
}
