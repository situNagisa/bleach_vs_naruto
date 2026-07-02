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

	::std::chrono::milliseconds step_seconds = ::std::chrono::milliseconds{1'000 / 30};
	::std::chrono::milliseconds accumulated_seconds = {};
};

struct preview_unit
{
	glm::vec3 position = {};
	glm::vec3 velocity = {};
	bool facing_right = true;
};

struct snapshot
{
	::std::uint64_t tick = 0;
	preview_unit unit;
};

struct preview_simulation
{
	std::uint64_t tick = 0;
	preview_unit unit;
};

/// Advance one fixed step. `move_dir` is the desired horizontal move direction
/// (x/z on the ground plane, magnitude 0..1); the hero stands still when it is zero.
void step(preview_simulation& simulation, glm::vec3 const& move_dir) noexcept;
auto capture(preview_simulation const& simulation) noexcept -> snapshot;

/// Previous/current snapshot pair for interpolation.
struct snapshot_buffer
{
	void publish(snapshot value) noexcept;
	auto previous() const noexcept -> snapshot const&;
	auto current() const noexcept -> snapshot const&;

	::std::array<snapshot, 2> snapshots = {};
};
}
