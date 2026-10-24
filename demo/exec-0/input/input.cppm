export module demo.input;

import std;
import demo.execution;
import demo.window;

export
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

export
struct key_status
{
	bool active : 1;
	bool signal : 1;
};

export struct input
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

	// 起始点：取得 _guard 后泵事件，按旧的 _current 计算本帧状态并写回，完成值是新状态的副本。
	// permit 在写回后即归还；并发的第二次 update 会等第一次写回完成。
	// 必须在主线程启动（GLFW 的事件泵约束）。
	[[nodiscard]] values_sender_type<state_type> update();

	// window 由调用方持有，必须比 input 活得久。
	explicit input(window const& target);

	using guard_type = ::nagisa::concurrency::binary_semaphore_guard;

	// 常量：构造后不变。
	window const* _window;
	// 变量：只在持有 _guard 的 permit 时读写。guard 不可移动，所以 input 也不可移动。
	state_type _current{};
	guard_type _guard{ 1 };
};
