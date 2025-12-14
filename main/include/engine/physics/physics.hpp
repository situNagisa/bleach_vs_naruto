#pragma once
#include <vector>
#include "collider.hpp"

struct CollisionPair {
    int a;
    int b;
};

class Physics {
public:
    virtual ~Physics() = default;

    virtual void clear() = 0;

    // Add colliders for this frame
    virtual int add_collider(const Collider& c) = 0;

    // Run collision detection
    virtual std::vector<CollisionPair> detect() = 0;
};
