#pragma once

#include <memory>

struct graphic
{
	struct renderer
	{
		virtual ~renderer() = default;

		virtual void draw_rect(
			int x, int y,
			int w, int h
		) = 0;
	};

	::std::unique_ptr<renderer>(*default_renderer)();
};