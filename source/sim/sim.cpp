#include <glm/geometric.hpp>

#include <bvn/sim/sim.h>

namespace bvn::sim
{

void step(preview_simulation& simulation, glm::vec3 const& move_dir) noexcept
{
	++simulation.tick;

	constexpr auto move_speed = 6.0f;            // world units per second
	constexpr auto step_seconds = 1.0f / 30.0f;  // matches the 30 Hz fixed step

	auto horizontal = glm::vec3{move_dir.x, 0.0f, move_dir.z};
	auto length = glm::length(horizontal);
	auto direction = length > 1.0e-4f ? horizontal / length : glm::vec3{0.0f};

	simulation.unit.velocity = direction * move_speed;
	simulation.unit.position += simulation.unit.velocity * step_seconds;

	if (direction.x > 1.0e-4f)
	{
		simulation.unit.facing_right = true;
	}
	else if (direction.x < -1.0e-4f)
	{
		simulation.unit.facing_right = false;
	}
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
