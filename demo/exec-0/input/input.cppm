module;

// MSVC 14.44 needs these declarations locally when importing constrained queries.
#include <stdexec/execution.hpp>

#include <bvn/platform/window.h>
#include <SDL3/SDL_init.h>

export module demo.input;

import std;
import demo.execution;

export
enum class key_code : ::std::uint8_t
{
	up,
	down,
	left,
	right,
	a,
	b,
	c,
	d,
	e,
	f,
	g,
	_max,
};

export
struct key_status
{
	// Held state and press edge relative to the last consumed snapshot.
	bool active : 1;
	bool signal : 1;
};

export struct input
{
	struct state_type
	{
		::std::array<key_status, ::std::to_underlying(key_code::_max)> _status{};
	};

	// Start on SDL's main thread; consume the result before the next poll.
	[[nodiscard]] values_sender_type<state_type> poll() const;
	void consume(state_type state);

	explicit input(::bvn::platform::window const& window);

	::SDL_WindowID _window_id;
	state_type _current{};
};
