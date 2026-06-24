#include <stdexcept>
#include <string>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <bvn/platform/sdl_context.h>

namespace
{
auto sdl_error() -> ::std::string
{
	auto const* message = SDL_GetError();
	if (message == nullptr || message[0] == '\0')
	{
		return "unknown SDL error";
	}

	return message;
}
}

namespace bvn::platform
{
sdl_context::sdl_context()
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		throw ::std::runtime_error("SDL_Init failed: " + sdl_error());
	}
}

sdl_context::~sdl_context() noexcept
{
	SDL_Quit();
}
}
