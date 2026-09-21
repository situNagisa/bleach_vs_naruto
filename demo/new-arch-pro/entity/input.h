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

#include "../framework/frame_context.h"
#include "../framework/entities.h"

/// 单窗口输入 entity。每帧首次启动 poll() 必须在 SDL 主线程；之后可跨线程订阅。
struct input
{
	/// 物理按键标识；编号由 input 定义，后端负责转换。
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

	/// 本次采集期间的边沿；按下再松开时，两者都为 true。
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
		/// 当前是否按住该按钮。
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

	template <class Poll>
	struct _senders
	{
		Poll _poll;

		/// split 持有两个值，向订阅者发出 (state const&, delta const&)。
		[[nodiscard]] auto&& poll() const noexcept { return _poll; }
	};

	template <class Poll>
	_senders(Poll) -> _senders<Poll>;

	struct _task_fn
	{
		auto operator()(input& source) const
		{
			auto poll = ::stdexec::just()
				| ::stdexec::let_value([&source]
					{
						auto changes = delta{};
						auto snapshot = source._poll(changes);
						return ::stdexec::just(::std::move(snapshot), ::std::move(changes));
					})
				| ::exec::split();
			return _senders{::std::move(poll)};
		}
	};

	using task = task_data<_task_fn, input&>;

	/// 绑定平台窗口；窗口及平台初始化必须覆盖 input 的使用期。
	explicit input(::bvn::platform::window const& window)
		: _window_id(::SDL_GetWindowID(window.handle))
	{
		assert(::SDL_IsMainThread());
		assert(_window_id != 0);
		if (::SDL_GetKeyboardFocus() == window.handle)
		{
			_sync_keyboard();
		}
		if (::SDL_GetMouseFocus() == window.handle)
		{
			auto const buttons = ::SDL_GetMouseState(&_current.mouse.position.x, &_current.mouse.position.y);
			for (auto index = ::std::size_t{}; index < _current.mouse._buttons.size(); ++index)
			{
				_current.mouse._buttons[index] = (buttons & SDL_BUTTON_MASK(_sdl_buttons[index])) != 0;
			}
		}
	}

	/// 仅登记共享输入 task 和根节点；构建本身不采集事件或另行登记 state。
	auto build_task(frame_context& context, entity_view<frame_context>, task_builder builder) -> void
	{
		auto&& result = builder.emplace<task>(*this);
		context.roots.emplace_back(result.poll() | ::stdexec::then([](state const&, delta const&) {}));
	}

	auto _poll(delta& changes) -> state
	{
		assert(::SDL_IsMainThread());
		auto event = ::SDL_Event{};
		while (::SDL_PollEvent(&event))
		{
			_process_event(event, changes);
		}
		// split 持有值副本，后续采集只更新 entity 内的跨帧状态。
		return _current;
	}

	auto _sync_keyboard() noexcept -> void
	{
		auto count = int{};
		auto const keys = ::SDL_GetKeyboardState(&count);
		for (auto index = ::std::size_t{}; index < _current.keyboard._keys.size(); ++index)
		{
			auto const code = _sdl_keys[index];
			_current.keyboard._keys[index] = code < count && keys[code];
		}
		auto const modifiers = ::SDL_GetModState();
		for (auto index = ::std::size_t{}; index < _sdl_modifiers.size(); ++index)
		{
			_current.keyboard._modifiers[index] = (modifiers & _sdl_modifiers[index]) != 0;
		}
	}

	static constexpr auto _update(bool& current, button_delta& change, bool down) noexcept -> void
	{
		change.pressed |= down && !current;
		change.released |= !down && current;
		current = down;
	}

	template <class Value, ::std::size_t Size>
	[[nodiscard]] static constexpr auto _index(::std::array<Value, Size> const& mapping, Value code) noexcept
		-> ::std::optional<::std::size_t>
	{
		auto const found = ::std::ranges::find(mapping, code);
		if (found == mapping.end())
		{
			return ::std::nullopt;
		}
		return static_cast<::std::size_t>(found - mapping.begin());
	}

	auto _process_event(::SDL_Event const& event, delta& changes) noexcept -> void
	{
		switch (event.type)
		{
		case ::SDL_EVENT_QUIT:
			changes.quit_requested |= !_current.quit_requested;
			_current.quit_requested = true;
			break;
		case ::SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			if (event.window.windowID == _window_id)
			{
				changes.quit_requested |= !_current.quit_requested;
				_current.quit_requested = true;
			}
			break;
		case ::SDL_EVENT_KEY_DOWN:
		case ::SDL_EVENT_KEY_UP:
			if (event.key.windowID == _window_id)
			{
				for (auto index = ::std::size_t{}; index < _sdl_modifiers.size(); ++index)
				{
					_update(_current.keyboard._modifiers[index], changes.keyboard._modifiers[index],
						(event.key.mod & _sdl_modifiers[index]) != 0);
				}
				if (auto const index = _index(_sdl_keys, event.key.scancode))
				{
					auto&& key = _current.keyboard._keys[*index];
					if (event.key.repeat)
					{
						key = true;
					}
					else
					{
						_update(key, changes.keyboard._keys[*index], event.type == ::SDL_EVENT_KEY_DOWN);
					}
				}
			}
			break;
		case ::SDL_EVENT_MOUSE_BUTTON_DOWN:
		case ::SDL_EVENT_MOUSE_BUTTON_UP:
			if (event.button.windowID == _window_id)
			{
				_current.mouse.position = {event.button.x, event.button.y};
				if (auto const index = _index(_sdl_buttons, static_cast<int>(event.button.button)))
				{
					_update(_current.mouse._buttons[*index], changes.mouse._buttons[*index],
						event.type == ::SDL_EVENT_MOUSE_BUTTON_DOWN);
				}
			}
			break;
		case ::SDL_EVENT_MOUSE_MOTION:
			if (event.motion.windowID == _window_id)
			{
				_current.mouse.position = {event.motion.x, event.motion.y};
				changes.mouse.motion.x += event.motion.xrel;
				changes.mouse.motion.y += event.motion.yrel;
			}
			break;
		case ::SDL_EVENT_MOUSE_WHEEL:
			if (event.wheel.windowID == _window_id)
			{
				auto const direction = event.wheel.direction == ::SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
				changes.mouse.wheel.x += direction * event.wheel.x;
				changes.mouse.wheel.y += direction * event.wheel.y;
			}
			break;
		case ::SDL_EVENT_WINDOW_FOCUS_LOST:
			if (event.window.windowID == _window_id)
			{
				// 失去焦点后，松开事件可能发往其他窗口，主动释放避免输入卡住。
				for (auto index = ::std::size_t{}; index < _current.keyboard._keys.size(); ++index)
				{
					_update(_current.keyboard._keys[index], changes.keyboard._keys[index], false);
				}
				for (auto index = ::std::size_t{}; index < _current.keyboard._modifiers.size(); ++index)
				{
					_update(_current.keyboard._modifiers[index], changes.keyboard._modifiers[index], false);
				}
				for (auto index = ::std::size_t{}; index < _current.mouse._buttons.size(); ++index)
				{
					_update(_current.mouse._buttons[index], changes.mouse._buttons[index], false);
				}
			}
			break;
		default:
			break;
		}
	}

	// 映射表的下标对应 input 自己的枚举；SDL 编号只在后端实现中使用。
	static constexpr auto _sdl_keys = ::std::array{
		::SDL_SCANCODE_A, ::SDL_SCANCODE_B, ::SDL_SCANCODE_C, ::SDL_SCANCODE_D, ::SDL_SCANCODE_E,
		::SDL_SCANCODE_F, ::SDL_SCANCODE_G, ::SDL_SCANCODE_H, ::SDL_SCANCODE_I, ::SDL_SCANCODE_J,
		::SDL_SCANCODE_K, ::SDL_SCANCODE_L, ::SDL_SCANCODE_M, ::SDL_SCANCODE_N, ::SDL_SCANCODE_O,
		::SDL_SCANCODE_P, ::SDL_SCANCODE_Q, ::SDL_SCANCODE_R, ::SDL_SCANCODE_S, ::SDL_SCANCODE_T,
		::SDL_SCANCODE_U, ::SDL_SCANCODE_V, ::SDL_SCANCODE_W, ::SDL_SCANCODE_X, ::SDL_SCANCODE_Y, ::SDL_SCANCODE_Z,
		::SDL_SCANCODE_0, ::SDL_SCANCODE_1, ::SDL_SCANCODE_2, ::SDL_SCANCODE_3, ::SDL_SCANCODE_4,
		::SDL_SCANCODE_5, ::SDL_SCANCODE_6, ::SDL_SCANCODE_7, ::SDL_SCANCODE_8, ::SDL_SCANCODE_9,
		::SDL_SCANCODE_RETURN, ::SDL_SCANCODE_ESCAPE, ::SDL_SCANCODE_BACKSPACE, ::SDL_SCANCODE_TAB, ::SDL_SCANCODE_SPACE,
		::SDL_SCANCODE_MINUS, ::SDL_SCANCODE_EQUALS, ::SDL_SCANCODE_LEFTBRACKET, ::SDL_SCANCODE_RIGHTBRACKET, ::SDL_SCANCODE_BACKSLASH,
		::SDL_SCANCODE_SEMICOLON, ::SDL_SCANCODE_APOSTROPHE, ::SDL_SCANCODE_GRAVE, ::SDL_SCANCODE_COMMA, ::SDL_SCANCODE_PERIOD, ::SDL_SCANCODE_SLASH,
		::SDL_SCANCODE_F1, ::SDL_SCANCODE_F2, ::SDL_SCANCODE_F3, ::SDL_SCANCODE_F4, ::SDL_SCANCODE_F5, ::SDL_SCANCODE_F6,
		::SDL_SCANCODE_F7, ::SDL_SCANCODE_F8, ::SDL_SCANCODE_F9, ::SDL_SCANCODE_F10, ::SDL_SCANCODE_F11, ::SDL_SCANCODE_F12,
		::SDL_SCANCODE_F13, ::SDL_SCANCODE_F14, ::SDL_SCANCODE_F15, ::SDL_SCANCODE_F16, ::SDL_SCANCODE_F17, ::SDL_SCANCODE_F18,
		::SDL_SCANCODE_F19, ::SDL_SCANCODE_F20, ::SDL_SCANCODE_F21, ::SDL_SCANCODE_F22, ::SDL_SCANCODE_F23, ::SDL_SCANCODE_F24,
		::SDL_SCANCODE_PRINTSCREEN, ::SDL_SCANCODE_SCROLLLOCK, ::SDL_SCANCODE_PAUSE, ::SDL_SCANCODE_INSERT, ::SDL_SCANCODE_HOME, ::SDL_SCANCODE_PAGEUP,
		::SDL_SCANCODE_DELETE, ::SDL_SCANCODE_END, ::SDL_SCANCODE_PAGEDOWN, ::SDL_SCANCODE_RIGHT, ::SDL_SCANCODE_LEFT, ::SDL_SCANCODE_DOWN, ::SDL_SCANCODE_UP,
		::SDL_SCANCODE_CAPSLOCK, ::SDL_SCANCODE_NUMLOCKCLEAR, ::SDL_SCANCODE_KP_DIVIDE, ::SDL_SCANCODE_KP_MULTIPLY,
		::SDL_SCANCODE_KP_MINUS, ::SDL_SCANCODE_KP_PLUS, ::SDL_SCANCODE_KP_ENTER,
		::SDL_SCANCODE_KP_0, ::SDL_SCANCODE_KP_1, ::SDL_SCANCODE_KP_2, ::SDL_SCANCODE_KP_3, ::SDL_SCANCODE_KP_4,
		::SDL_SCANCODE_KP_5, ::SDL_SCANCODE_KP_6, ::SDL_SCANCODE_KP_7, ::SDL_SCANCODE_KP_8, ::SDL_SCANCODE_KP_9,
		::SDL_SCANCODE_KP_PERIOD, ::SDL_SCANCODE_KP_EQUALS, ::SDL_SCANCODE_APPLICATION,
		::SDL_SCANCODE_LCTRL, ::SDL_SCANCODE_LSHIFT, ::SDL_SCANCODE_LALT, ::SDL_SCANCODE_LGUI,
		::SDL_SCANCODE_RCTRL, ::SDL_SCANCODE_RSHIFT, ::SDL_SCANCODE_RALT, ::SDL_SCANCODE_RGUI,
	};
	static constexpr auto _sdl_modifiers = ::std::array{
		SDL_KMOD_LSHIFT, SDL_KMOD_RSHIFT, SDL_KMOD_LCTRL, SDL_KMOD_RCTRL,
		SDL_KMOD_LALT, SDL_KMOD_RALT, SDL_KMOD_LGUI, SDL_KMOD_RGUI,
		SDL_KMOD_NUM, SDL_KMOD_CAPS, SDL_KMOD_SCROLL, SDL_KMOD_MODE, SDL_KMOD_LEVEL5,
	};
	static constexpr auto _sdl_buttons = ::std::array{
		SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT, SDL_BUTTON_X1, SDL_BUTTON_X2,
	};
	static_assert(_sdl_keys.size() == ::std::to_underlying(key_code::count));
	static_assert(_sdl_modifiers.size() == ::std::to_underlying(modifier::count));
	static_assert(_sdl_buttons.size() == ::std::to_underlying(mouse_button::count));

	::SDL_WindowID _window_id;
	state _current{};
};
