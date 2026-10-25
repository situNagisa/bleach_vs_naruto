module;

#include <GLFW/glfw3.h>

export module demo.menu;

import std;
import demo.execution;
import demo.window;
import demo.input;

enum class key_code : ::std::uint8_t
{
	quit,
	up,
	down,
	confirm,
	_max,
};

inline constexpr auto key_count = ::std::to_underlying(key_code::_max);

struct selector
{
	selector(window& w, ::std::size_t size) 
		: window(w)
		, size(size)
	{}

	void update();
	
	window& window;
	::std::array<toggle_button, key_count> keyboard{};
	::std::size_t size{};
	::std::size_t index{};
};

module : private;


inline constexpr auto keys_impl = ::std::array{
	GLFW_KEY_ESCAPE,
	GLFW_KEY_UP,
	GLFW_KEY_DOWN,
	GLFW_KEY_ENTER,
};
static_assert(keys_impl.size() == key_count);

void selector::update()
{
	window::poll_events();
	if (!window.focused())
		return;

	for (auto const [index, key] : keys_impl | ::std::views::enumerate)
	{
		keyboard[index].apply(window.key_pressed(key));
	}
	if (window.should_close())
		keyboard[::std::to_underlying(key_code::quit)].apply(true);

	if (keyboard[::std::to_underlying(key_code::down)].change == toggle_button::change_type::to_press)
	{
		++index;
	}

	if (keyboard[::std::to_underlying(key_code::up)].change == toggle_button::change_type::to_press)
	{
		index += size;
		--index;
	}
	index %= size;

}