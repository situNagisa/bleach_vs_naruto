module;

// 薄包装：声明的唯一来源是 menu.h（README §3）。这里只提供依赖并把声明导出。
// export extern "C++"：声明附着到全局模块，所以普通翻译单元 menu.cpp 里的定义合法。

#include <vulkan/vulkan.h>
#include <vkkl/command_buffer.h>

export module demo.scene.menu;

import std;
import demo.execution;
export import demo.renderable.buffer_type;
import demo.input;
import demo.render;

export extern "C++"
{
#define BVN_EXEC0_MODULE_INTERFACE
#include "menu.h"
#undef BVN_EXEC0_MODULE_INTERFACE
}
