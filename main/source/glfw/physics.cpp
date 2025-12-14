// simple AABB physics/collision detector
#include "engine/physics/physics.hpp"
#include <vector>

struct PhysicsSimple : public Physics {
    std::vector<Collider> colliders;

    void clear() override {
        colliders.clear();
    }

    int add_collider(const Collider& c) override {
        colliders.push_back(c);
        return (int)colliders.size() - 1;
    }

    static bool aabb_overlap(const Rect& a, const Rect& b) {
        return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
    }

    std::vector<CollisionPair> detect() override {
        std::vector<CollisionPair> out;
        int n = (int)colliders.size();
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (colliders[i].type == ColliderType::AABB && colliders[j].type == ColliderType::AABB) {
                    if (aabb_overlap(colliders[i].aabb, colliders[j].aabb)) {
                        out.push_back({ i,j });
                    }
                }
            }
        }
        return out;
    }
};
