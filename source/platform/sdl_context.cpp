#include <stdexcept>
#include <string>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <bvn/platform/sdl_context.h>

namespace bvn::platform
{
sdl_context::sdl_context()
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		auto const* message = SDL_GetError();
		throw ::std::runtime_error{message == nullptr || message[0] == '\0' ? "SDL_Init failed: unknown SDL error" : "SDL_Init failed: " + ::std::string{message}};
	}
}

sdl_context::~sdl_context() noexcept
{
	SDL_Quit();
}
}
