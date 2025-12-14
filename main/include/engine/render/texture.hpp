#pragma once
#include <string>

using TextureHandle = uint32_t; // abstract id

struct TextureDesc {
    int width = 0;
    int height = 0;
};
