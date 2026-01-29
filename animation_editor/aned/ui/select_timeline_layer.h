#pragma once

#include <cstddef>

namespace aned::component
{
	struct select_timeline_layer
	{
		constexpr static auto unselect = static_cast<::std::size_t>(-1);
		::std::size_t index{ 0 };
	};
}
