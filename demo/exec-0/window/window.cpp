#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "../common/std.h"
#include "window.h"

namespace
{
[[noreturn]] void throw_glfw_error(char const* operation)
{
	char const* message = nullptr;
	::glfwGetError(&message);
	throw ::std::runtime_error{
		message == nullptr || message[0] == '\0'
			? ::std::string{ operation } + " failed: unknown GLFW error"
			: ::std::string{ operation } + " failed: " + message };
}
}

glfw_context::glfw_context()
{
	if (!::glfwInit())
		throw_glfw_error("glfwInit");
	::glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
}

glfw_context::~glfw_context() noexcept
{
	::glfwTerminate();
}

window::window(char const* title, int width, int height)
{
	if (title == nullptr)
		throw ::std::invalid_argument("window title must not be null");
	if (width <= 0 || height <= 0)
		throw ::std::invalid_argument("window extent must be positive");

	handle = ::glfwCreateWindow(width, height, title, nullptr, nullptr);
	if (handle == nullptr)
		throw_glfw_error("glfwCreateWindow");
}

window::~window() noexcept
{
	if (handle != nullptr)
		::glfwDestroyWindow(handle);
}

window::window(window&& other) noexcept
	: handle(::std::exchange(other.handle, nullptr))
{
}

window& window::operator=(window&& other) noexcept
{
	if (this != &other)
	{
		if (handle != nullptr)
			::glfwDestroyWindow(handle);
		handle = ::std::exchange(other.handle, nullptr);
	}
	return *this;
}

::std::vector<char const*> window::required_vulkan_extensions() const
{
	auto count = ::std::uint32_t{};
	auto const extensions = ::glfwGetRequiredInstanceExtensions(&count);
	if (extensions == nullptr)
		throw_glfw_error("glfwGetRequiredInstanceExtensions");
	return ::std::vector<char const*>(extensions, extensions + count);
}

VkSurfaceKHR window::vulkan_surface(VkInstance instance) const
{
	if (instance == VK_NULL_HANDLE)
		throw ::std::invalid_argument("VkInstance must not be null");

	auto surface = VkSurfaceKHR{ VK_NULL_HANDLE };
	if (::glfwCreateWindowSurface(instance, handle, nullptr, &surface) != VK_SUCCESS)
		throw_glfw_error("glfwCreateWindowSurface");
	return surface;
}

window_extent window::drawable_extent() const
{
	auto width = int{};
	auto height = int{};
	::glfwGetFramebufferSize(handle, &width, &height);
	if (width < 0 || height < 0)
		throw ::std::runtime_error{ "GLFW returned a negative framebuffer size" };
	return {
		.width = static_cast<::std::uint32_t>(width),
		.height = static_cast<::std::uint32_t>(height),
	};
}

void window::poll_events()
{
	::glfwPollEvents();
}

bool window::should_close() const noexcept
{
	return ::glfwWindowShouldClose(handle) == GLFW_TRUE;
}

bool window::key_pressed(int key) const noexcept
{
	return ::glfwGetKey(handle, key) == GLFW_PRESS;
}

bool window::focused() const noexcept
{
	return ::glfwGetWindowAttrib(handle, GLFW_FOCUSED) == GLFW_TRUE;
}
