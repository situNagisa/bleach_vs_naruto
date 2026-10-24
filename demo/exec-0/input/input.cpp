module;

// 只为 GLFW_KEY_* 常量；窗口本身通过 demo.window 访问。
#include <GLFW/glfw3.h>

module demo.input;

input::input(window const& target)
	: _window(::std::addressof(target))
{
}

values_sender_type<input::state_type> input::poll() const
{
	return {
		::stdexec::just()
		| ::stdexec::then([this]
			{
				window::poll_events();

				auto state = state_type{};
					state.close_requested = _window->should_close();

				static constexpr auto keys = ::std::array{
					GLFW_KEY_ESCAPE,
					GLFW_KEY_UP, GLFW_KEY_DOWN,
					GLFW_KEY_LEFT, GLFW_KEY_RIGHT,
					GLFW_KEY_J, GLFW_KEY_K, GLFW_KEY_L,
					GLFW_KEY_U, GLFW_KEY_I, GLFW_KEY_O,
					GLFW_KEY_P,
						GLFW_KEY_ENTER,
				};
				static_assert(keys.size() == ::std::to_underlying(key_code::_max));

				if (!_window->focused())
					return state;

				for (auto index = ::std::size_t{}; index < keys.size(); ++index)
				{
					auto const active = _window->key_pressed(keys[index]);
					state._status[index] = {
						.active = active,
						.signal = active && !_current._status[index].active,
					};
				}
					auto const confirm = _window->key_pressed(GLFW_KEY_ENTER)
						|| _window->key_pressed(GLFW_KEY_KP_ENTER) || _window->key_pressed(GLFW_KEY_SPACE);
					state._status[::std::to_underlying(key_code::confirm)] = {
						.active = confirm, .signal = confirm && !_current[key_code::confirm].active };

				return state;
			})
	};
}

void input::consume(state_type state)
{
	_current = state;
}
