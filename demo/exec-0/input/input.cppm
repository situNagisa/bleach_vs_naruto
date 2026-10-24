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

// 一次 update 内某个键的变化：按下 / 抬起 / 不变。
export
enum class key_change : ::std::uint8_t
{
	none,
	pressed,
	released,
};

export struct input
{
	static constexpr auto key_count = ::std::to_underlying(key_code::_max);

	// 持久状态：本次 update 之后每个键是否按着，以及窗口是否已请求关闭。
	struct state_type
	{
		::std::array<bool, key_count> pressed{};
		bool close_requested = false;

		[[nodiscard]] constexpr bool operator[](key_code key) const noexcept
		{
			return pressed[::std::to_underlying(key)];
		}
	};

	// 变化值：本次 update 相对上一次 state 的差异。close_requested 只在关闭请求新出现的那次为 true。
	struct change_type
	{
		::std::array<key_change, key_count> keys{};
		bool close_requested = false;

		[[nodiscard]] constexpr key_change operator[](key_code key) const noexcept
		{
			return keys[::std::to_underlying(key)];
		}
	};

	using guard_type = ::nagisa::concurrency::binary_semaphore_guard;

	// 起始点：取得 guard 的 permit 后泵事件，按 state 计算新状态并写入 state，完成值是 (permit, 变化值)。
	// input 不析构 permit：调用方在本帧不再读 state 之后自行销毁（或移交），在此之前并发的第二次 update 不会完成。
	// 必须在主线程启动（GLFW 的事件泵约束）。
	[[nodiscard]] values_sender_type<guard_type::permit, change_type> update();

	// window 由调用方持有，必须比 input 活得久。
	explicit input(window const& target);

	// 常量：构造后不变。
	window const* _window;
	// 开放访问：只有持有 guard 的 permit 时才能读写 state。guard 不可移动，所以 input 也不可移动。
	state_type state{};
	guard_type guard{ 1 };
};
