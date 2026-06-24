#include <stdexcept>
#include <string>
#include <utility>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

#include <bvn/platform/window.h>

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

auto checked_extent_component(int value, char const* name) -> ::std::uint32_t
{
	if (value < 0)
	{
		throw ::std::runtime_error(::std::string("SDL returned a negative ") + name);
	}

	return static_cast<::std::uint32_t>(value);
}
}

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
	handle_ = SDL_CreateWindow(title, width, height, flags);
	if (handle_ == nullptr)
	{
		throw ::std::runtime_error("SDL_CreateWindow failed: " + sdl_error());
	}
}

window::~window() noexcept
{
	if (handle_ != nullptr)
	{
		SDL_DestroyWindow(handle_);
	}
}

window::window(window&& other) noexcept
	: handle_(::std::exchange(other.handle_, nullptr))
{
}

auto window::operator=(window&& other) noexcept -> window&
{
	if (this != &other)
	{
		if (handle_ != nullptr)
		{
			SDL_DestroyWindow(handle_);
		}

		handle_ = ::std::exchange(other.handle_, nullptr);
	}

	return *this;
}

auto window::native() const noexcept -> SDL_Window*
{
	return handle_;
}

auto window::required_vulkan_extensions() const -> ::std::vector<char const*>
{
	Uint32 count = 0;
	auto const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
	if (extensions == nullptr)
	{
		throw ::std::runtime_error("SDL_Vulkan_GetInstanceExtensions failed: " + sdl_error());
	}

	return ::std::vector<char const*>(extensions, extensions + count);
}

auto window::create_vulkan_surface(VkInstance instance) const -> VkSurfaceKHR
{
	if (instance == VK_NULL_HANDLE)
	{
		throw ::std::invalid_argument("VkInstance must not be null");
	}

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if (!SDL_Vulkan_CreateSurface(handle_, instance, nullptr, &surface))
	{
		throw ::std::runtime_error("SDL_Vulkan_CreateSurface failed: " + sdl_error());
	}

	return surface;
}

auto window::drawable_extent() const -> window_extent
{
	int width = 0;
	int height = 0;
	if (!SDL_GetWindowSizeInPixels(handle_, &width, &height))
	{
		throw ::std::runtime_error("SDL_GetWindowSizeInPixels failed: " + sdl_error());
	}

	return window_extent
	{
		.width = checked_extent_component(width, "drawable width"),
		.height = checked_extent_component(height, "drawable height"),
	};
}
}
