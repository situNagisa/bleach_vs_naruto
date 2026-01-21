#pragma once

#include <ranges>
#include <vector>
#include <variant>
#include <algorithm>

#include "./frame.h"

struct timeline
{
	struct keyframe
	{

	};

	struct frame_span
	{
		::std::size_t hold_frame = 0;
	};

	::std::vector<::std::tuple<keyframe, frame_span, ::std::size_t /* prefix sum */ >> _keyframes{};
	constexpr auto _prefix_sum() const { return _keyframes | ::std::views::elements<2>; }

	struct keyframe_observer
	{
		keyframe* _data{};
	};
	struct frame_span_observer
	{
		frame_span* _data{};
	};
	using frame = ::std::variant<keyframe_observer, frame_span_observer>;
	
	auto frames() const
	{
		auto frame_count = static_cast<::std::size_t>(0u);
		if (!_keyframes.empty())
		{
			auto&& [kf, dur, psum] = _keyframes.back();
			frame_count = psum + dur.hold_frame + 1u;
		}
		return ::std::views::iota(0u, frame_count) | ::std::views::transform([this](::std::size_t idx)
			{
				auto it = ::std::ranges::lower_bound(_prefix_sum(), idx);
				auto keyframe_index = static_cast<::std::size_t>(::std::ranges::distance(this->_prefix_sum().begin(), it));
				auto&& [kf, dur, psum] = _keyframes[keyframe_index];
				if (*it == idx)
				{
					return frame{ keyframe_observer{ &kf } };
				}
				return frame{ frame_span_observer{ &dur } };
			});
	}
};