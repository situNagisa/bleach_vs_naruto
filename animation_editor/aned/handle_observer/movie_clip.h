#pragma once

#include <ranges>

#include <entt/entt.hpp>

#include <boost/assert.hpp>

#include "../asset/movie_clip.h"
#include "../movie/play_data.h"

namespace aned::handle_observer
{
	struct movie_clip
	{
		inline static const auto default_layer = []
			{
				timeline_system::timeline_layer layer{ "Main Timeline", ::aned::timeline_system::timeline() };
				layer.timeline.emplace_back();
				return layer;
			}();

		::entt::handle _handle{};

		// movie_clip() noexcept = default;
		movie_clip(::entt::handle h) : _handle(h)
		{}

		auto&& timeline_system() const noexcept { return _handle.get<component::timeline_system>(); }
		auto&& current_layer(component::play_data const& data) const noexcept
		{
			auto&& system = timeline_system();
			BOOST_ASSERT(!::std::ranges::empty(system.layers()));
			BOOST_ASSERT(data.current_layer < ::std::ranges::size(system.layers()));
			return system.layers()[data.current_layer];
		}
		auto&& current_frame(component::play_data const& data) const noexcept
		{
			auto&& layer = current_layer(data);
			BOOST_ASSERT(!::std::ranges::empty(layer.timeline));
			BOOST_ASSERT(data.current_frame < ::std::ranges::size(layer.timeline));
			return layer.timeline[data.current_frame];
		}
		auto handle() const noexcept { return _handle; }
	};
}