#pragma once

#include <chrono>

#include "../io.h"
#include "../graphic.h"
#include "../physical.h"

struct player
{
	virtual ~player() = default;
	virtual void process_io(io::keyboard& keyboard) {}
	virtual void process_graphic(graphic::renderer& renderer) {}
	virtual void process_time(::std::chrono::milliseconds delta) {}
};