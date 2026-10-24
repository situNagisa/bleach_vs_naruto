module;

// 薄包装：声明的唯一来源是 buffer_type.h（README §3）。这里只提供依赖并把声明导出。
// export extern "C++"：声明附着到全局模块，所以普通翻译单元 buffer_type.cpp 里的定义合法。

#include <vulkan/vulkan.h>

export module demo.renderable.buffer_type;

import std;

export extern "C++"
{
#define BVN_EXEC0_MODULE_INTERFACE
#include "buffer_type.h"
#undef BVN_EXEC0_MODULE_INTERFACE
}
