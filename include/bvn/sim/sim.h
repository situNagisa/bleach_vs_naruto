#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>

#include <glm/vec3.hpp>

namespace bvn::sim
{
/// Fixed-step accumulator for the 30 Hz simulation loop.
struct fixed_step_clock
{
	constexpr auto set_fps(::std::size_t fps) noexcept
	{
		step_seconds = ::std::chrono::milliseconds{ 1'000 / fps };
	}

	constexpr auto add_frame_time(::std::chrono::milliseconds frame_time) noexcept
	{
		accumulated_seconds += frame_time;
	}
	constexpr auto should_step() const noexcept
	{
		return accumulated_seconds >= step_seconds;
	}
	constexpr auto interpolation_alpha() const noexcept -> double
	{
		if (step_seconds.count() == 0)
		{
			return 0.0;
		}

		auto alpha = static_cast<double>(accumulated_seconds.count()) / static_cast<double>(step_seconds.count());
		return alpha > 1.0 ? 1.0 : alpha;
	}
	constexpr auto consume_step() noexcept
	{
		if (accumulated_seconds >= step_seconds)
		{
			accumulated_seconds -= step_seconds;
		}
	}
private:
	::std::chrono::milliseconds step_seconds = ::std::chrono::milliseconds{1'000 / 30};
	::std::chrono::milliseconds accumulated_seconds = {};
};

struct preview_unit
{
public:
	glm::vec3 position = {};
	bool facing_right = true;
};

struct snapshot
{
public:
	::std::uint64_t tick = 0;
	preview_unit unit;
};

struct preview_simulation
{
public:
	std::uint64_t tick = 0;
	preview_unit unit;
};

void step(preview_simulation& simulation) noexcept;
auto capture(preview_simulation const& simulation) noexcept -> snapshot;

/// Previous/current snapshot pair for interpolation.
struct snapshot_buffer
{
public:
	void publish(snapshot value) noexcept;
	auto previous() const noexcept -> snapshot const&;
	auto current() const noexcept -> snapshot const&;

private:
	::std::array<snapshot, 2> snapshots = {};
};
}
