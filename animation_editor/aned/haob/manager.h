#pragma once

#include <entt/entt.hpp>

#include "./display.h"
#include "./movie_clip.h"
#include "./image.h"

namespace aned::haob
{
	struct handle_manager
	{
		::entt::registry world{};

		auto display_object() noexcept
		{
			auto entity = world.create();
			world.emplace<component::display_node>(entity);
			return haob::display_object{ ::entt::handle{ world, entity } };
		}
		auto movie_clip() noexcept
		{
			auto mc = haob::movie_clip{ display_object().handle() };
			mc.handle().emplace<component::timeline_system>();
			mc.handle().emplace<component::play_data>();
			return mc;
		}
		auto image() noexcept
		{
			auto img = haob::image{ display_object().handle() };
			img.handle().emplace<component::image>();
			return img;
		}
	};
}