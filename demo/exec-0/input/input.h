// demo.input 的声明，唯一来源。三种展开形态见 execution/execution.h 开头与 README §3。
#if defined(BVN_EXEC0_MODULES) && !defined(BVN_EXEC0_MODULE_INTERFACE)

import std;
import demo.execution;
import demo.window;
import demo.input;

#else

#if !defined(BVN_EXEC0_MODULE_INTERFACE)
#include <array>
#include <cstdint>
#include <utility>

#include "../execution/execution.h"
#include "../window/window.h"
#endif

#ifndef BVN_EXEC0_INPUT_INPUT_H
#define BVN_EXEC0_INPUT_INPUT_H

enum class key_code : ::std::uint8_t
{
	quit,
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
	confirm,
	_max,
};

struct key_status
{
	bool active : 1;
	bool signal : 1;
};

struct input
{
	struct state_type
	{
		::std::array<key_status, ::std::to_underlying(key_code::_max)> _status{};
		bool close_requested = false;

		[[nodiscard]] constexpr key_status const& operator[](key_code key) const noexcept
		{
			return _status[::std::to_underlying(key)];
		}
	};

	// 必须在主线程启动（GLFW 的事件泵约束）；下一次 poll 之前先 consume 结果。
	[[nodiscard]] values_sender_type<state_type> poll() const;
	void consume(state_type state);

	// window 由调用方持有，必须比 input 活得久。
	explicit input(window const& target);

	window const* _window;
	state_type _current{};
};

#endif

#endif
