// very small OpenGL renderer using immediate mode
#include "engine/render/renderer.hpp"
#include <GLFW/glfw3.h>
#include <cstdio>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

struct RendererGL : public Renderer {
    int win_w = 800, win_h = 600;
    GLFWwindow* window = nullptr;

    RendererGL(GLFWwindow* w, int w_, int h_) : window(w), win_w(w_), win_h(h_) {}

    void begin_frame(int width, int height) override {
        win_w = width; win_h = height;
        glViewport(0, 0, width, height);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width, height, 0, -1, 1); // origin top-left
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClearColor(0.09f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void draw_rect(const Rect& r, const Color& c) override {
        glColor4f(c.r, c.g, c.b, c.a);
        glBegin(GL_QUADS);
        glVertex2f(r.x, r.y);
        glVertex2f(r.x + r.w, r.y);
        glVertex2f(r.x + r.w, r.y + r.h);
        glVertex2f(r.x, r.y + r.h);
        glEnd();
    }

    void end_frame() override {
        // swap buffers done in main loop (we have access to window)
    }
};
