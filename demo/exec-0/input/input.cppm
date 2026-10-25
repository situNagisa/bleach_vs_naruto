export module demo.input;

import std;
import demo.execution;
import demo.window;

export
struct toggle_button
{
	enum class change_type : ::std::uint8_t
	{
		no_change,
		to_press,
		to_release,
	};

	bool is_pressed : 1;
	bool hover : 1;
	change_type change : 2;

	constexpr void apply(bool pressed) noexcept
	{
		if (is_pressed == pressed)
			change = change_type::no_change;
		else if (is_pressed && !pressed)
			change = change_type::to_release;
		else if (!is_pressed && pressed)
			change = change_type::to_press;
		is_pressed = pressed;
	}
};

export
struct tri_state_button
{
	enum class state_type : ::std::uint8_t
	{
		unchecked,
		checked,
		indeterminate,
	};

	enum class change_type : ::std::uint8_t
	{
		no_change,
		to_unchecked,
		to_checked,
		to_indeterminate,
	};

	state_type state : 2;
	change_type change : 3;
	bool hover : 1;

	constexpr void apply(state_type new_state) noexcept
	{
		if (state == new_state)
			change = change_type::no_change;
		else if (new_state == state_type::unchecked)
			change = change_type::to_unchecked;
		else if (new_state == state_type::checked)
			change = change_type::to_checked;
		else if (new_state == state_type::indeterminate)
			change = change_type::to_indeterminate;
		state = new_state;
	}
};

module : private;
