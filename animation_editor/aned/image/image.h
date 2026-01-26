#pragma once

#include <cstddef>

#include "../opengl.h"

namespace aned::component
{
	struct image
	{
		opengl::raii_texture texture{};
		::std::size_t width = 0;
		::std::size_t height = 0;
	};
}



#if 0
int hitTest(float mx, float my) override {
	float scaled_w = width * scaleX;
	float scaled_h = height * scaleY;
	float x1 = x;
	float y1 = y;
	float x2 = x1 + scaled_w;
	float y2 = y1 + scaled_h;

	if (mx >= x1 && mx < x2 && my >= y1 && my < y2) {
		return id;
	}
	return -1;
}
#endif