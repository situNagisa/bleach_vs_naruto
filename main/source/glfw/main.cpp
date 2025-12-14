// main entry: wire everything up, create 2 entities that can move (arrow keys)
#include "engine/platform/platform.hpp"
#include "engine/render/renderer.hpp"
#include "engine/physics/physics.hpp"
#include "engine/game/world.hpp"
#include "engine/game/controller.hpp"
#include "common/math.hpp"

#include "./platform.cpp" // simple approach to keep single translation unit for demo
#include "./renderer.cpp"
#include "./physics.cpp"

#include <GLFW/glfw3.h>
#include <chrono>
#include <thread>

// simple controller: move with arrow keys
struct PlayerController : public Controller {
    void update(Entity& self, const InputState& input, float dt) override {
        Vec2 dir{ 0,0 };
        if (input.left.down) dir.x -= 1.0f;
        if (input.right.down) dir.x += 1.0f;
        if (input.up.down) dir.y -= 1.0f;
        if (input.down.down) dir.y += 1.0f;
        // normalize-ish
        if (dir.x != 0 || dir.y != 0) {
            float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
            dir.x /= len; dir.y /= len;
        }
        self.position.x += dir.x * self.speed * dt;
        self.position.y += dir.y * self.speed * dt;
    }
};

int main() {
    WindowDesc desc;
    desc.width = 800; desc.height = 600; desc.title = "Minimal Fighter - Prototype";

    // platform
    PlatformGLFW* platform = new PlatformGLFW(desc);
    if (!platform->should_close() && !glfwGetCurrentContext()) {
        // ensure context
    }
    GLFWwindow* window = glfwGetCurrentContext();

    // renderer (pass window and size)
    RendererGL* renderer = new RendererGL(window, desc.width, desc.height);

    // physics
    PhysicsSimple* phys = new PhysicsSimple();

    // world
    World world;
    world.platform = platform;
    world.renderer = renderer;
    world.physics = phys;

    // create two entities
    Entity e1; e1.id = 0; e1.position = { 100,100 }; e1.size = { 120,120 }; e1.speed = 300;
    Entity e2; e2.id = 1; e2.position = { 400,250 }; e2.size = { 120,120 }; e2.speed = 0;

    world.entities.push_back(e1);
    world.entities.push_back(e2);

    // controllers: only controller[0] moves by input; controller[1] is nullptr (static)
    PlayerController* pc = new PlayerController();
    world.controllers.resize(2, nullptr);
    world.controllers[0] = pc;

    double last_t = platform->time_now();
    while (!platform->should_close()) {
        platform->poll_events();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

        double now = platform->time_now();
        float dt = (float)(now - last_t);
        if (dt > 0.033f) dt = 0.033f; // clamp
        last_t = now;

        world.update(dt);
        world.resolve_collisions();

        // begin render
        renderer->begin_frame(desc.width, desc.height);
        world.render();
        renderer->end_frame();

        glfwSwapBuffers(window);

        // tiny sleep to avoid spinning too hard
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    delete pc;
    delete renderer;
    delete phys;
    delete platform;

    return 0;
}
