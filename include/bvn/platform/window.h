#pragma once

#include <cstdint>
#include <vector>

#include <SDL3/SDL_video.h>
#include <vulkan/vulkan.h>

namespace bvn::platform
{
/**
 * Drawable size in framebuffer pixels.
 */
struct window_extent
{
	::std::uint32_t width = 0;
	::std::uint32_t height = 0;
};

/**
 * RAII wrapper for an SDL window configured for Vulkan presentation.
 */
struct window
{
	window(char const* title, int width, int height);
	~window() noexcept;

	window(window const&) = delete;
	auto operator=(window const&) -> window& = delete;

	window(window&& other) noexcept;
	auto operator=(window&& other) noexcept -> window&;

	/**
	 * Returns the Vulkan instance extensions SDL requires for this platform.
	 */
	auto required_vulkan_extensions() const -> ::std::vector<char const*>;

	/**
	 * Creates a Vulkan surface for the window.
	 *
	 * The caller owns the returned surface and must destroy it before the window.
	 */
	auto vulkan_surface(VkInstance instance) const -> VkSurfaceKHR;

	/**
	 * Returns the current drawable extent in framebuffer pixels.
	 */
	auto drawable_extent() const -> window_extent;

	SDL_Window* handle = nullptr;
};
}
