#pragma once

#include <cstddef>

#include "../project_config.h"

namespace aned::component
{
	struct play_data
	{
		::std::size_t current_frame{};
		bool play{};
		::std::size_t frame_rate = project_config::frame_rate;
	};
}
