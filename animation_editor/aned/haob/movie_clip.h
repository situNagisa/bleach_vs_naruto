#pragma once


#include <entt/entt.hpp>

#include <boost/assert.hpp>

#include "../asset/movie_clip.h"
#include "../movie/play_data.h"

#include "./display.h"

namespace aned::haob
{
	struct movie_clip : display_object
	{
		inline static const auto default_layer = []
			{
				asset::movie_clip::layer layer{ "Main Timeline", ::aned::timeline_system::timeline() };
				layer.timeline.emplace_back();
				return layer;
			}();

		using display_object::display_object;

		auto valid() const noexcept { return display_object::valid() && _handle.all_of<component::timeline_system, component::play_data>(); }

		auto&& timeline_system() const noexcept { return _handle.get<component::timeline_system>(); }
		auto&& play_data() const noexcept { return _handle.get<component::play_data>(); }


		auto&& current_layer() const noexcept
		{
			BOOST_ASSERT(valid());
			auto&& system = timeline_system();
			BOOST_ASSERT(!::std::ranges::empty(system->layers()));
			BOOST_ASSERT(play_data().current_layer < ::std::ranges::size(system->layers()));
			return system->layers()[play_data().current_layer];
		}
		auto current_frame() const noexcept
		{
			BOOST_ASSERT(valid());
			auto&& layer = current_layer();
			BOOST_ASSERT(!::std::ranges::empty(layer.timeline));
			BOOST_ASSERT(play_data().current_frame < ::std::ranges::size(layer.timeline));
			return layer.timeline[play_data().current_frame];
		}
		auto add_child(::entt::handle child) noexcept
		{
			BOOST_ASSERT(valid());
			BOOST_ASSERT(child.valid());
			current_frame().keyframe->displays.emplace_back(child);
			auto dp = haob::display_object(child);
			if (!dp.valid())
				return;
			dp.parent() = _handle;
		}
	};
}