// demo.window 的声明，唯一来源。三种展开形态见 execution/execution.h 开头与 README §3。
#if defined(BVN_EXEC0_MODULES) && !defined(BVN_EXEC0_MODULE_INTERFACE)

import std;
import demo.window;

#else

#if !defined(BVN_EXEC0_MODULE_INTERFACE)
#include <cstdint>
#include <vector>

// 先包含 Vulkan 头再包含 glfw3.h：GLFW 只有在看到 VK_VERSION_1_0 之后才声明 Vulkan 相关函数。
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#endif

#ifndef BVN_EXEC0_WINDOW_WINDOW_H
#define BVN_EXEC0_WINDOW_WINDOW_H

// 进程级 GLFW 初始化（glfwInit / glfwTerminate），必须在任何 window 之前构造、之后析构。
// 全局提示 GLFW_CLIENT_API = GLFW_NO_API：窗口只用于 Vulkan 呈现，不创建 OpenGL 上下文。
struct glfw_context
{
	glfw_context();
	~glfw_context() noexcept;

	glfw_context(glfw_context const&) = delete;
	glfw_context& operator=(glfw_context const&) = delete;
	glfw_context(glfw_context&&) = delete;
	glfw_context& operator=(glfw_context&&) = delete;
};

// 帧缓冲像素尺寸。
struct window_extent
{
	::std::uint32_t width = 0;
	::std::uint32_t height = 0;
};

// 只移动的 GLFW 窗口所有者。所有成员函数都要在主线程调用（GLFW 的约束）。
struct window
{
	window(char const* title, int width, int height);
	~window() noexcept;

	window(window const&) = delete;
	window& operator=(window const&) = delete;
	window(window&& other) noexcept;
	window& operator=(window&& other) noexcept;

	// Vulkan 实例需要启用的扩展（GLFW 的 glfwGetRequiredInstanceExtensions）。
	::std::vector<char const*> required_vulkan_extensions() const;
	// 调用方拥有返回的 surface，必须在窗口销毁前销毁它。
	VkSurfaceKHR vulkan_surface(VkInstance instance) const;
	window_extent drawable_extent() const;

	// 事件处理：泵一次事件队列；关闭请求；按键是否按下（GLFW_KEY_*）；窗口是否有键盘焦点。
	static void poll_events();
	bool should_close() const noexcept;
	bool key_pressed(int key) const noexcept;
	bool focused() const noexcept;

	GLFWwindow* handle = nullptr;
};

#endif

#endif
