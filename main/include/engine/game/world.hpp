#pragma once
#include <vector>
#include "../render/renderer.hpp"
#include "../physics/physics.hpp"
#include "entity.hpp"
#include "controller.hpp"
#include "../platform/platform.hpp"

struct World {
    Renderer* renderer = nullptr;
    Physics* physics = nullptr;
    Platform* platform = nullptr;

    std::vector<Entity> entities;
    std::vector<Controller*> controllers;

    void update(float dt) {
        if (!platform) return;
        InputState in = platform->input();

        // basic: update controllers and update colliders
        physics->clear();
        for (auto& e : entities) {
            // controller update if exists (match by index)
            if (e.id >= 0 && e.id < (int)controllers.size() && controllers[e.id]) {
                controllers[e.id]->update(e, in, dt);
            }
            // update collider
            Collider c;
            c.type = ColliderType::AABB;
            c.aabb = { e.position.x, e.position.y, e.size.x, e.size.y };
            e.collider_id = physics->add_collider(c);
            e.collided = false;
        }
    }

    void resolve_collisions() {
        auto pairs = physics->detect();
        for (auto& p : pairs) {
            if (p.a >= 0 && p.a < (int)entities.size()) entities[p.a].collided = true;
            if (p.b >= 0 && p.b < (int)entities.size()) entities[p.b].collided = true;
        }
    }

    void render() {
        if (!renderer) return;
        // assume window size set by platform
        // draw each entity as rect (color changes on collision)
        for (auto& e : entities) {
            Rect r{ e.position.x, e.position.y, e.size.x, e.size.y };
            if (e.collided) renderer->draw_rect(r, { 1.0f, 0.2f, 0.2f, 1.0f });
            else renderer->draw_rect(r, { 0.2f, 0.8f, 0.2f, 1.0f });
        }
    }
};
