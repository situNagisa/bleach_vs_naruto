#pragma once

#include <entt/entt.hpp>

#include "./display_object.h"
#include "./container.h"

struct display_object_container : display_object
{
	display_object_container(::entt::handle handle)
		: display_object(handle)
	{
		handle.emplace<display_container>();
	}
};