module;

// 薄包装：声明的唯一来源是 window.h（README §3）。这里只提供依赖并把声明导出。
// export extern "C++"：声明附着到全局模块，所以普通翻译单元 window.cpp 里的定义合法。

// 先包含 Vulkan 头再包含 glfw3.h：GLFW 只有在看到 VK_VERSION_1_0 之后才声明 Vulkan 相关函数。
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

export module demo.window;

import std;

export extern "C++"
{
#define BVN_EXEC0_MODULE_INTERFACE
#include "window.h"
#undef BVN_EXEC0_MODULE_INTERFACE
}
