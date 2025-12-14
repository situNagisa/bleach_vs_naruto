#pragma once
#include "common/math.hpp"

enum class ColliderType {
    AABB,
    Circle,
    Polygon
};

struct Collider {
    ColliderType type = ColliderType::AABB;
    Rect aabb;     // only for AABB (extend as needed)
};
