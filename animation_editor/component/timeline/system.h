#pragma once

#include <vector>
#include <ranges>
#include <algorithm>

#include "./timeline.h"
#include "./layer.h"

struct timeline_system
{
	::std::vector<timeline_layer> _layers{};

	auto layers() const { return _layers | ::std::views::all; }
	auto frames() const noexcept
	{
		using namespace ::std::views;
		auto size = ::std::ranges::max(_layers | transform(&timeline_layer::timeline) | transform(::std::ranges::size));
		return iota(0u, size)
			| transform([this](::std::size_t index) noexcept
				{
					return _layers | transform([index](timeline_layer const& layer) noexcept
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

