module;

// 薄包装：声明的唯一来源是 sprite.h（README §3）。这里只提供依赖并把声明导出。
// export extern "C++"：声明附着到全局模块，所以普通翻译单元 sprite.cpp 里的定义合法。

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <bvn/assets/sprite_clip.h>

export module demo.renderable.sprite;

import std;
export import demo.renderable.buffer_type;

export extern "C++"
{
#define BVN_EXEC0_MODULE_INTERFACE
#include "sprite.h"
#undef BVN_EXEC0_MODULE_INTERFACE
}
