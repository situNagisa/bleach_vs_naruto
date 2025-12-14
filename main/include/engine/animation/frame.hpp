#pragma once
#include <vector>
#include "common/math.hpp"
#include "engine/physics/collider.hpp"

struct Frame {
    int duration = 1; // #frames this frame lasts
    Rect texture_region;
    std::vector<Collider> hitboxes;
    std::vector<Collider> hurtboxes;
};
