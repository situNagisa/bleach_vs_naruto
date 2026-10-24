module;

// 薄包装：声明的唯一来源是 renderer.h（README §3）。这里只提供依赖并把声明导出。
// export extern "C++"：声明附着到全局模块，所以普通翻译单元 renderer.cpp 里的定义合法。

#include <vulkan/vulkan.h>

#include <nagisa/concurrency/semaphore.h>

export module demo.render;

import std;
import demo.execution;
import demo.window;

export extern "C++"
{
#define BVN_EXEC0_MODULE_INTERFACE
#include "renderer.h"
#undef BVN_EXEC0_MODULE_INTERFACE
}
