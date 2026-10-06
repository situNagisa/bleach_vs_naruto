#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include <bvn/platform/window.h>

#include <nagisa/concurrency/_manual_lifetime.h>

#include "../framework/frame_context.h"
#include "../framework/any_sender.h"

struct input
{
	enum class key_code : ::std::uint16_t
	{
		a, b, c, d, e, f, g, h, i, j, k, l, m,
		n, o, p, q, r, s, t, u, v, w, x, y, z,
		digit_0, digit_1, digit_2, digit_3, digit_4,
		digit_5, digit_6, digit_7, digit_8, digit_9,
		enter, escape, backspace, tab, space,
		minus, equal, left_bracket, right_bracket, backslash,
		semicolon, apostrophe, grave, comma, period, slash,
		f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
		f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24,
		print_screen, scroll_lock, pause, insert, home, page_up,
		delete_key, end, page_down, right, left, down, up,
		caps_lock, num_lock, keypad_divide, keypad_multiply,
		keypad_minus, keypad_plus, keypad_enter,
		keypad_0, keypad_1, keypad_2, keypad_3, keypad_4,
		keypad_5, keypad_6, keypad_7, keypad_8, keypad_9,
		keypad_period, keypad_equal, application,
		left_control, left_shift, left_alt, left_super,
		right_control, right_shift, right_alt, right_super,
		count,
	};

	enum class modifier : ::std::uint8_t
	{
		left_shift, right_shift, left_control, right_control,
		left_alt, right_alt, left_super, right_super,
		num_lock, caps_lock, scroll_lock, mode, level5,
		count,
	};

	enum class mouse_button : ::std::uint8_t
	{
		left, middle, right, extra1, extra2,
		count,
	};

	struct vector2
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct button_delta
	{
		bool pressed = false;
		bool released = false;
	};

	struct keyboard_state
	{
		/// 当前是否按住该物理按键。
		[[nodiscard]] constexpr auto key(key_code code) const noexcept -> bool
		{
			assert(code < key_code::count);
			return _keys[static_cast<::std::size_t>(code)];
		}

		/// 当前修饰键或锁定状态是否生效。
		[[nodiscard]] constexpr auto has_modifier(modifier code) const noexcept -> bool
		{
			assert(code < modifier::count);
			return _modifiers[static_cast<::std::size_t>(code)];
		}

		::std::array<bool, ::std::to_underlying(key_code::count)> _keys{};
		::std::array<bool, ::std::to_underlying(modifier::count)> _modifiers{};
	};

	struct mouse_state
	{
		[[nodiscard]] constexpr auto button(mouse_button code) const noexcept -> bool
		{
			assert(code < mouse_button::count);
			return _buttons[static_cast<::std::size_t>(code)];
		}

		vector2 position{}; ///< 窗口坐标，原点在左上角，x 向右、y 向下。
		::std::array<bool, ::std::to_underlying(mouse_button::count)> _buttons{};
	};

	/// entity 持有的跨帧状态；poll() 的第一个完成值是采集后的值副本。
	struct state
	{
		keyboard_state keyboard{};
		mouse_state mouse{};
		bool quit_requested = false;
	};

	struct keyboard_delta
	{
		/// 本次采集的按下/松开边沿；自动重复不产生新的 pressed。
		[[nodiscard]] constexpr auto key(key_code code) const noexcept -> button_delta const&
		{
			assert(code < key_code::count);
			return _keys[static_cast<::std::size_t>(code)];
		}

		[[nodiscard]] constexpr auto modifier_change(modifier code) const noexcept -> button_delta const&
		{
			assert(code < modifier::count);
			return _modifiers[static_cast<::std::size_t>(code)];
		}

		::std::array<button_delta, ::std::to_underlying(key_code::count)> _keys{};
		::std::array<button_delta, ::std::to_underlying(modifier::count)> _modifiers{};
	};

	struct mouse_delta
	{
		[[nodiscard]] constexpr auto button(mouse_button code) const noexcept -> button_delta const&
		{
			assert(code < mouse_button::count);
			return _buttons[static_cast<::std::size_t>(code)];
		}

		vector2 motion{}; ///< 本次采集累计相对位移，x 向右、y 向下。
		vector2 wheel{}; ///< 本次采集累计滚轮量，x 向右、y 向上。
		::std::array<button_delta, ::std::to_underlying(mouse_button::count)> _buttons{};
	};

	/// poll() 的第二个完成值；只在本次采集中累积，不存入 entity。
	struct delta
	{
		keyboard_delta keyboard{};
		mouse_delta mouse{};
		bool quit_requested = false; ///< 本次采集首次收到退出请求。
	};

	struct task
	{
		decltype(::exec::split(::std::declval<values_sender_type<state, delta>>())) _poll;

		[[nodiscard]] auto&& poll() const noexcept { return _poll; }

		explicit task(input& input);
	};

	/// 绑定平台窗口；窗口及平台初始化必须覆盖 input 的使用期。
	explicit input(::bvn::platform::window const& window);

	void build_task(task& t);

	::SDL_WindowID _window_id;
	state _current{};
};
