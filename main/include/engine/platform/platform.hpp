#pragma once
#include <string>
#include <vector>
#include "common/math.hpp"

struct KeyState {
    bool down = false;
    bool pressed = false;   // this frame
    bool released = false;  // this frame
};

struct InputState {
    KeyState left, right, up, down;
    KeyState attack1, attack2, jump;
};

struct WindowDesc {
    int width = 1280;
    int height = 720;
    std::string title = "Game";
};

class Platform {
public:
    virtual ~Platform() = default;

    virtual void poll_events() = 0;
    virtual bool should_close() const = 0;

    virtual InputState input() const = 0;
    virtual double time_now() const = 0;

    // no rendering API here
};
