#pragma once

#include <vector>
#include <ranges>
#include <algorithm>
#include <variant>

#include "./timeline.h"
#include "./layer.h"

struct timeline_system
{
	::std::vector<timeline_layer> _layers{};

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
	auto keyframes() const noexcept
	{
		using namespace ::std::views;
		return frames()
			| transform([](auto&& frame_views) noexcept
				{
					constexpr auto transformer = [](::std::optional<timeline::iterator_impl<true>::frame> const& o) -> ::std::optional<timeline::keyframe>
						{
							if (!o)
								return ::std::nullopt;
							switch (o->index())
							{
							case 0:
								return ::std::make_optional(::std::get<0>(*o).keyframe());
							case 1:
								return ::std::make_optional(::std::get<1>(*o).keyframe());
							default:
								return ::std::nullopt;
							}
						};
					return frame_views | transform(transformer);
				});
	}
};

