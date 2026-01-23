#pragma once

#include <string>

#include "./timeline.h"

struct timeline_layer
{
	::std::string name{};
	timeline timeline{};
};