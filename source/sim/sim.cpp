#include <cmath>

#include <bvn/sim/sim.h>

namespace bvn::sim
{

void step(preview_simulation& simulation) noexcept
{
	auto previous_x = simulation.unit.position.x;
	++simulation.tick;

	auto period = 180.0f;
	auto phase = std::fmod(static_cast<float>(simulation.tick), period) / period;
	auto triangle = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
	auto next_x = -3.0f + triangle * 6.0f;

	simulation.unit.position = {next_x, 0.0f, 0.0f};
	simulation.unit.facing_right = next_x >= previous_x;
}

auto capture(preview_simulation const& simulation) noexcept -> snapshot
{
	return snapshot{.tick = simulation.tick, .unit = simulation.unit};
}

void snapshot_buffer::publish(snapshot value) noexcept
{
	snapshots[0] = snapshots[1];
	snapshots[1] = value;
}

auto snapshot_buffer::previous() const noexcept -> snapshot const&
{
	return snapshots[0];
}

auto snapshot_buffer::current() const noexcept -> snapshot const&
{
	return snapshots[1];
}
}
