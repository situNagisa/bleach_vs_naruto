export module demo.input;

import std;
import demo.execution;
import demo.window;

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
		// 本次轮询期间窗口收到了关闭请求。
		bool _quit = false;

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
