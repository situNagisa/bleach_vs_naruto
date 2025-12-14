#pragma once
#include "entity.hpp"
#include "engine/platform/platform.hpp"

class Controller {
public:
    virtual ~Controller() = default;
    virtual void update(Entity& self, const InputState& input, float dt) = 0;
};
