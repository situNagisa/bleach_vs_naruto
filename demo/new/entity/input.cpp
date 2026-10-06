
#include "./input.h"

// 映射表的下标对应 input 自己的枚举；SDL 编号只在后端实现中使用。
static constexpr auto sdl_keys = ::std::array{
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
static constexpr auto sdl_modifiers = ::std::array{
	SDL_KMOD_LSHIFT, SDL_KMOD_RSHIFT, SDL_KMOD_LCTRL, SDL_KMOD_RCTRL,
	SDL_KMOD_LALT, SDL_KMOD_RALT, SDL_KMOD_LGUI, SDL_KMOD_RGUI,
	SDL_KMOD_NUM, SDL_KMOD_CAPS, SDL_KMOD_SCROLL, SDL_KMOD_MODE, SDL_KMOD_LEVEL5,
};
static constexpr auto sdl_buttons = ::std::array{
	SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT, SDL_BUTTON_X1, SDL_BUTTON_X2,
};
static_assert(sdl_keys.size() == ::std::to_underlying(input::key_code::count));
static_assert(sdl_modifiers.size() == ::std::to_underlying(input::modifier::count));
static_assert(sdl_buttons.size() == ::std::to_underlying(input::mouse_button::count));

static constexpr auto _update(bool& current, input::button_delta& change, bool down) noexcept -> void
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

input::task::task(input& input)
	: _poll(values_sender_type<state, delta>{::stdexec::just()
		| ::stdexec::let_value([&]
			{
				auto changes = delta{};
				assert(::SDL_IsMainThread());
				auto event = ::SDL_Event{};
				while (::SDL_PollEvent(&event))
				{
					switch (event.type)
					{
					case ::SDL_EVENT_QUIT:
						changes.quit_requested |= !input._current.quit_requested;
						input._current.quit_requested = true;
						break;
					case ::SDL_EVENT_WINDOW_CLOSE_REQUESTED:
						if (event.window.windowID == input._window_id)
						{
							changes.quit_requested |= !input._current.quit_requested;
							input._current.quit_requested = true;
						}
						break;
					case ::SDL_EVENT_KEY_DOWN:
					case ::SDL_EVENT_KEY_UP:
						if (event.key.windowID == input._window_id)
						{
							for (auto index = ::std::size_t{}; index < sdl_modifiers.size(); ++index)
							{
								_update(input._current.keyboard._modifiers[index], changes.keyboard._modifiers[index],
									(event.key.mod & sdl_modifiers[index]) != 0);
							}
							if (auto const index = _index(sdl_keys, event.key.scancode))
							{
								auto&& key = input._current.keyboard._keys[*index];
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
						if (event.button.windowID == input._window_id)
						{
							input._current.mouse.position = { event.button.x, event.button.y };
							if (auto const index = _index(sdl_buttons, static_cast<int>(event.button.button)))
							{
								_update(input._current.mouse._buttons[*index], changes.mouse._buttons[*index],
									event.type == ::SDL_EVENT_MOUSE_BUTTON_DOWN);
							}
						}
						break;
					case ::SDL_EVENT_MOUSE_MOTION:
						if (event.motion.windowID == input._window_id)
						{
							input._current.mouse.position = { event.motion.x, event.motion.y };
							changes.mouse.motion.x += event.motion.xrel;
							changes.mouse.motion.y += event.motion.yrel;
						}
						break;
					case ::SDL_EVENT_MOUSE_WHEEL:
						if (event.wheel.windowID == input._window_id)
						{
							auto const direction = event.wheel.direction == ::SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
							changes.mouse.wheel.x += direction * event.wheel.x;
							changes.mouse.wheel.y += direction * event.wheel.y;
						}
						break;
					case ::SDL_EVENT_WINDOW_FOCUS_LOST:
						if (event.window.windowID == input._window_id)
						{
							// 失去焦点后，松开事件可能发往其他窗口，主动释放避免输入卡住。
							for (auto index = ::std::size_t{}; index < input._current.keyboard._keys.size(); ++index)
							{
								_update(input._current.keyboard._keys[index], changes.keyboard._keys[index], false);
							}
							for (auto index = ::std::size_t{}; index < input._current.keyboard._modifiers.size(); ++index)
							{
								_update(input._current.keyboard._modifiers[index], changes.keyboard._modifiers[index], false);
							}
							for (auto index = ::std::size_t{}; index < input._current.mouse._buttons.size(); ++index)
							{
								_update(input._current.mouse._buttons[index], changes.mouse._buttons[index], false);
							}
						}
						break;
					default:
						break;
					}
				}
				return ::stdexec::just(input._current, ::std::move(changes));
			})
		} | ::exec::split())
{

}

input::input(::bvn::platform::window const& window)
	: _window_id(::SDL_GetWindowID(window.handle))
{
	assert(::SDL_IsMainThread());
	assert(_window_id != 0);
	if (::SDL_GetKeyboardFocus() == window.handle)
	{
		auto count = int{};
		auto const keys = ::SDL_GetKeyboardState(&count);
		for (auto index = ::std::size_t{}; index < _current.keyboard._keys.size(); ++index)
		{
			auto const code = sdl_keys[index];
			_current.keyboard._keys[index] = code < count && keys[code];
		}
		auto const modifiers = ::SDL_GetModState();
		for (auto index = ::std::size_t{}; index < sdl_modifiers.size(); ++index)
		{
			_current.keyboard._modifiers[index] = (modifiers & sdl_modifiers[index]) != 0;
		}
	}
	if (::SDL_GetMouseFocus() == window.handle)
	{
		auto const buttons = ::SDL_GetMouseState(&_current.mouse.position.x, &_current.mouse.position.y);
		for (auto index = ::std::size_t{}; index < _current.mouse._buttons.size(); ++index)
		{
			_current.mouse._buttons[index] = (buttons & SDL_BUTTON_MASK(sdl_buttons[index])) != 0;
		}
	}
}

void input::build_task(task& t)
{
}
