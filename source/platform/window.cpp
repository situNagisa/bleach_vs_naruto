#include <stdexcept>
#include <string>
#include <utility>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

#include <bvn/platform/window.h>

namespace bvn::platform
{
window::window(char const* title, int width, int height)
{
	if (title == nullptr)
	{
		throw ::std::invalid_argument("window title must not be null");
	}

	if (width <= 0 || height <= 0)
	{
		throw ::std::invalid_argument("window extent must be positive");
	}

	auto const flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	handle = SDL_CreateWindow(title, width, height, flags);
	if (handle == nullptr)
	{
		auto const* message = SDL_GetError();
		throw ::std::runtime_error{message == nullptr || message[0] == '\0' ? "SDL_CreateWindow failed: unknown SDL error" : "SDL_CreateWindow failed: " + ::std::string{message}};
	}
}

window::~window() noexcept
{
	if (handle != nullptr)
	{
		SDL_DestroyWindow(handle);
	}
}

window::window(window&& other) noexcept
	: handle(::std::exchange(other.handle, nullptr))
{
}

auto window::operator=(window&& other) noexcept -> window&
{
	if (this != &other)
	{
		if (handle != nullptr)
		{
			SDL_DestroyWindow(handle);
		}

		handle = ::std::exchange(other.handle, nullptr);
	}

	return *this;
}

auto window::required_vulkan_extensions() const -> ::std::vector<char const*>
{
	Uint32 count = 0;
	auto const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
	if (extensions == nullptr)
	{
		auto const* message = SDL_GetError();
		throw ::std::runtime_error{message == nullptr || message[0] == '\0' ? "SDL_Vulkan_GetInstanceExtensions failed: unknown SDL error" : "SDL_Vulkan_GetInstanceExtensions failed: " + ::std::string{message}};
	}

	return ::std::vector<char const*>(extensions, extensions + count);
}

auto window::vulkan_surface(VkInstance instance) const -> VkSurfaceKHR
{
	if (instance == VK_NULL_HANDLE)
	{
		throw ::std::invalid_argument("VkInstance must not be null");
	}

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if (!SDL_Vulkan_CreateSurface(handle, instance, nullptr, &surface))
	{
		auto const* message = SDL_GetError();
		throw ::std::runtime_error{message == nullptr || message[0] == '\0' ? "SDL_Vulkan_CreateSurface failed: unknown SDL error" : "SDL_Vulkan_CreateSurface failed: " + ::std::string{message}};
	}

	return surface;
}

auto window::drawable_extent() const -> window_extent
{
	int width = 0;
	int height = 0;
	if (!SDL_GetWindowSizeInPixels(handle, &width, &height))
	{
		auto const* message = SDL_GetError();
		throw ::std::runtime_error{message == nullptr || message[0] == '\0' ? "SDL_GetWindowSizeInPixels failed: unknown SDL error" : "SDL_GetWindowSizeInPixels failed: " + ::std::string{message}};
	}

	if (width < 0)
	{
		throw ::std::runtime_error{"SDL returned a negative drawable width"};
	}

	if (height < 0)
	{
		throw ::std::runtime_error{"SDL returned a negative drawable height"};
	}

	return window_extent
	{
		.width = static_cast<::std::uint32_t>(width),
		.height = static_cast<::std::uint32_t>(height),
	};
}
}
