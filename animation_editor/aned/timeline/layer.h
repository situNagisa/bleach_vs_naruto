#pragma once

#include <string>

#include "./timeline.h"

namespace aned::timeline_system
{
	struct timeline_layer
	{
		::std::string name{};
		timeline timeline{};
	};
}
