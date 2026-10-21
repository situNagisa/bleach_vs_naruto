module;

#include <cassert>

// Keep the stdexec declarations visible before importing demo.input on MSVC 14.44.
#include <stdexec/execution.hpp>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

module demo.input;

input::input(::bvn::platform::window const& window)
	: _window_id(::SDL_GetWindowID(window.handle))
{
	
}

values_sender_type<input::state_type> input::poll() const
{
	return {
		::stdexec::just()
		| ::stdexec::then([this]
			{
				assert(::SDL_IsMainThread());
				::SDL_PumpEvents();

				static constexpr auto keys = ::std::array{
					::SDL_SCANCODE_UP, ::SDL_SCANCODE_DOWN,
					::SDL_SCANCODE_LEFT, ::SDL_SCANCODE_RIGHT,
					::SDL_SCANCODE_J, ::SDL_SCANCODE_K, ::SDL_SCANCODE_L,
					::SDL_SCANCODE_U, ::SDL_SCANCODE_I, ::SDL_SCANCODE_O,
					::SDL_SCANCODE_P,
				};
				static_assert(keys.size() == ::std::to_underlying(key_code::_max));

				auto state = state_type{};
				auto const focus = ::SDL_GetKeyboardFocus();
				if (focus == nullptr || ::SDL_GetWindowID(focus) != _window_id)
					return state;

				auto count = int{};
				auto const keyboard = ::SDL_GetKeyboardState(&count);
				for (auto index = ::std::size_t{}; index < keys.size(); ++index)
				{
					auto const active = keys[index] < count && keyboard[keys[index]];
					state._status[index] = {
						.active = active,
						.signal = active && !_current._status[index].active,
					};
				}
				return state;
			})
	};
}

void input::consume(state_type state)
{
	_current = state;
}
