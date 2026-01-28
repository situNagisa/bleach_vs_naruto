#pragma once

#include <vector>
#include <ranges>
#include <algorithm>
#include <variant>

#include "./timeline.h"
#include "./layer.h"

namespace aned::component
{
	struct timeline_system
	{
		using timeline_layer = aned::timeline_system::timeline_layer;
		using timeline = aned::timeline_system::timeline;
		::std::vector<timeline_layer> _layers{};

		constexpr static auto _empty_displays = ::std::span<::entt::handle const>{};

		auto layers() const { return _layers | ::std::views::all; }
		auto frames() const noexcept
		{
			using namespace ::std::views;
			auto size = _layers.empty() ? 0 : ::std::ranges::max(_layers | transform(&timeline_layer::timeline) | transform(::std::ranges::size));
			return iota(0u, size)
				| transform([this](::std::size_t index) noexcept
					{
						return _layers | transform([index](timeline_layer const& layer) noexcept -> ::std::optional<timeline::iterator_impl<true>::frame>
							{
								auto&& tl = layer.timeline;
								if (index < ::std::ranges::size(tl))
								{
									return ::std::make_optional(*::std::ranges::next(tl.begin(), index));
								}
								return ::std::nullopt;
							});
					});
		}
	};


}
