#pragma once

#include <vector>
#include <ranges>
#include <algorithm>

#include "../timeline/timeline.h"

namespace aned::asset
{
	struct movie_clip
	{
		struct layer
		{
			::std::string name{};
			timeline_system::timeline timeline{};
		};
		::std::vector<layer> _layers{};

		auto layers() noexcept { return _layers | ::std::views::all; }
		auto layers() const noexcept { return _layers | ::std::views::all; }
		auto frames() const noexcept
		{
			using namespace ::std::views;
			auto size = _layers.empty() ? 0 : ::std::ranges::max(_layers | transform(&layer::timeline) | transform(::std::ranges::size));
			return iota(0u, size)
				| transform([this](::std::size_t index) noexcept
					{
						return _layers | transform([index](layer const& layer) noexcept -> ::std::optional<timeline_system::timeline::iterator_impl<true>::frame>
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
