// implementation of Platform using GLFW
#include "engine/platform/platform.hpp"
#include <GLFW/glfw3.h>
#include <chrono>
#include <memory>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

struct PlatformGLFW : public Platform {
    GLFWwindow* window = nullptr;
    InputState inputState;
    double start_time = 0.0;
    int width = 800, height = 600;

    PlatformGLFW(const WindowDesc& desc) {
        glfwInit();
        // use GL 2.1 so immediate-mode is available on many platforms
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
        width = desc.width; height = desc.height;
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        start_time = glfwGetTime();
    }
    ~PlatformGLFW() {
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
    }
    void poll_events() override {
        glfwPollEvents();
        // map some keys to InputState
        inputState.left.down = glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS;
        inputState.right.down = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;
        inputState.up.down = glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS;
        inputState.down.down = glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS;
        inputState.attack1.down = glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS;
        // pressed/released are naive (edge detection can be added)
    }
    bool should_close() const override {
        return window && glfwWindowShouldClose(window);
    }
    InputState input() const override {
        return inputState;
    }
    double time_now() const override {
        return glfwGetTime() - start_time;
    }
};
