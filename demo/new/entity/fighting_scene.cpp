#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "./fighting_scene.h"

fighting_scene::operation_state::operation_state(fighting_scene& scene, input& i, input::operation_state::shared_state controls, renderer& r, renderer::operation_state::shared_state draw)
	: _update(values_sender_type<state>{ controls.poll()
		| ::stdexec::then([&scene](input::state const&, input::delta const& changes)
			{
				scene._state.back_requested |= changes.keyboard.key(input::key_code::escape).pressed;
				return scene._state;
			}) } | ::exec::split())
{}

fighting_scene::operation_state::shared_state fighting_scene::operation_state::start(frame_context& context) & noexcept
{
	context.roots.emplace_back(_update | ::stdexec::then([](state const&) noexcept {}));
	return shared_state(*this);
}

fighting_scene::fighting_scene(input& i, renderer& r)
{
}
