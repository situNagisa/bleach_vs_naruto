#pragma once
#include "texture.hpp"
#include "common/math.hpp"

struct Sprite {
    TextureHandle texture;
    Rect source;   // in texture
    Vec2 origin;   // pivot
};

struct Color { float r, g, b, a; };

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void begin_frame(int width, int height) = 0;
    virtual void draw_rect(const Rect& r, const Color& c) = 0;
    virtual void end_frame() = 0;
};
