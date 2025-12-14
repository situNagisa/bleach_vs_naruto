#pragma once
#include <string>
#include "common/math.hpp"
#include "engine/animation/animation.hpp"

struct Entity {
    int id = -1;
    Vec2 position;
    Vec2 size{ 50,50 }; // width/height default
    float speed = 200.0f; // units per second
    // runtime fields
    bool collided = false;
    int collider_id = -1;
};
