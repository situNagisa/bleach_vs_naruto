#pragma once

#include <chrono>


struct physical_component
{
	struct d2
	{
		float x{}, y{};
	}position{}, velocity{}, acceleration{};
};

