#pragma once


#include <entt/entt.hpp>

#include <boost/assert.hpp>

#include "../asset/image.h"
#include "./display.h"

namespace aned::haob
{
	struct image : display_object
	{
		using display_object::display_object;

		auto valid() const noexcept { return display_object::valid() && _handle.all_of<component::image>(); }

		auto&& image_component() const noexcept { return _handle.get<component::image>(); }
	};
}