#pragma once

#include "./image.h"

#include "../timeline/system.h"

namespace aned::asset
{
	struct movie_clip
	{
		component::timeline_system timeline_system{};
	};
}