#pragma once

#include <cstddef>

namespace aned::component
{
	struct play_data
	{
		::std::size_t current_frame{};
		bool play{};
	};
}
