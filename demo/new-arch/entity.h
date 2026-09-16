#pragma once

#include "./frame_context.h"
#include "./entities.h"

struct entity
{
	auto build_task(frame_context& context, entity_view<frame_context>, task_builder builder) -> void
	{
	}
};