#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include <stdexec/execution.hpp>

#include "../framework/any_sender.h"

/// 帧时钟。每帧 `tick` 一次，给出本帧序号和距上一帧的间隔。
struct frame_clock
{
	using clock_type = ::std::chrono::steady_clock;

	struct frame_time
	{
		::std::uint64_t index;           ///< 本帧序号，从 0 开始
		clock_type::duration delta;      ///< 距上一帧 tick 的间隔；第 0 帧为 0
		clock_type::time_point now;      ///< 本帧 tick 的时刻
	};

	[[nodiscard]] values_sender_type<frame_time> tick()
	{
		return ::stdexec::just()
			| ::stdexec::then([this]
				{
					auto const now = clock_type::now();
					auto const result = frame_time{
						.index = _index++,
						.delta = _last ? now - *_last : clock_type::duration::zero(),
						.now = now,
					};
					_last = now;
					return result;
				});
	}

	/// 已经 tick 过的帧数。
	[[nodiscard]] auto index() const noexcept -> ::std::uint64_t { return _index; }

	::std::uint64_t _index = 0;
	::std::optional<clock_type::time_point> _last{};
};
