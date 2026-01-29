#pragma once

#include <boost/geometry.hpp>

namespace aned::component
{
	struct hit_box
	{
		::boost::geometry::model::box<::boost::geometry::model::d2::point_xy<float>> box;
	};
	struct hit_box_depend_children_tag{};
}