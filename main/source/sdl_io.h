#pragma once

#include <SDL2/SDL.h>

#include "./io.h"

struct sdl_keyboard : io::keyboard
{
	// uses SDL_GetKeyboardState: no window handle required
	const uint8_t* state = nullptr;

	sdl_keyboard()
	{
		// returns pointer to internal SDL state array; ensure SDL_Init called before using
		state = SDL_GetKeyboardState(nullptr);
	}

	inline bool is_scancode(SDL_Scancode sc) const
	{
		if (!state) return false;
		return state[sc] != 0;
	}

	bool a() const override { return is_scancode(SDL_SCANCODE_A); }
	bool d() const override { return is_scancode(SDL_SCANCODE_D); }
	bool w() const override { return is_scancode(SDL_SCANCODE_W); }
	bool s() const override { return is_scancode(SDL_SCANCODE_S); }

	bool j() const override { return is_scancode(SDL_SCANCODE_J); }
	bool k() const override { return is_scancode(SDL_SCANCODE_K); }
	bool l() const override { return is_scancode(SDL_SCANCODE_L); }

	bool u() const override { return is_scancode(SDL_SCANCODE_U); }
	bool i() const override { return is_scancode(SDL_SCANCODE_I); }
	bool o() const override { return is_scancode(SDL_SCANCODE_O); }
};
inline ::std::unique_ptr<io::keyboard> default_keyboard()
{
	return std::make_unique<sdl_keyboard>();
}