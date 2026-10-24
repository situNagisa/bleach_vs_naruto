module;

// 薄包装：声明的唯一来源是 font.h（README §3）。这里只提供依赖并把声明导出。
// export extern "C++"：声明附着到全局模块，所以普通翻译单元 font.cpp 里的定义合法。

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

export module demo.renderable.font;

import std;
export import demo.renderable.buffer_type;

export extern "C++"
{
#define BVN_EXEC0_MODULE_INTERFACE
#include "font.h"
#undef BVN_EXEC0_MODULE_INTERFACE
}
