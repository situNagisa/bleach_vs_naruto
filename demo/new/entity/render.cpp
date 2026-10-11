#include <cassert>
#include <stdexcept>
#include <string>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include "./render.h"

void renderer::set_fullscreen(bool enabled)
{
	assert(::SDL_IsMainThread());
	if (((::SDL_GetWindowFlags(window.handle) & SDL_WINDOW_FULLSCREEN) != 0) == enabled)
		return;
	::consumer_arch_vulkan::check(::vkDeviceWaitIdle(vulkan.global_env().device()), "fullscreen: wait for device");
	if (!::SDL_SetWindowFullscreen(window.handle, enabled) || !::SDL_SyncWindow(window.handle))
		throw ::std::runtime_error{ ::std::string{ "fullscreen: " } + ::SDL_GetError() };
	::consumer_arch_vulkan::create_swapchain(vulkan, window);
}
