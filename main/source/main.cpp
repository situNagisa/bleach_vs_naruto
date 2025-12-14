#include <bvn/infra.h>
#include <nagisa/symbol_loader/core.h>
#include <nagisa/symbol_loader/_loader/native.h>

#include <nagisa/concurrency/concurrency.h>

#include <stdexec/execution.hpp>
#include <GLFW/glfw3.h>

struct renderer
{
	virtual ~renderer() = default;

	virtual void begin_frame() = 0;
	virtual void draw_rect(int x, int y, int w, int h) = 0;
	virtual void end_frame() = 0;
};

struct glfw_renderer : renderer
{
	GLFWwindow* win;

	glfw_renderer(int width = 800, int height = 600)
	{
		glfwInit();
		win = glfwCreateWindow(width, height, "Game", nullptr, nullptr);
		glfwMakeContextCurrent(win);
		glOrtho(0, width, height, 0, -1, 1);
	}

	~glfw_renderer() override
	{
		glfwDestroyWindow(win);
		glfwTerminate();
	}

	void begin_frame() override
	{
		glfwMakeContextCurrent(win);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	void draw_rect(int x, int y, int w, int h) override
	{
		glBegin(GL_QUADS);
		glVertex2i(x, y);
		glVertex2i(x + w, y);
		glVertex2i(x + w, y + h);
		glVertex2i(x, y + h);
		glEnd();
	}

	void end_frame() override
	{
		glfwSwapBuffers(win);
		glfwPollEvents();
	}
};

std::unique_ptr<renderer> default_renderer()
{
	return std::make_unique<glfw_renderer>();
}

struct physic
{
	
};

struct io
{
	struct keyboard
	{
		virtual ~keyboard() = default;

		virtual bool a() const = 0;
		virtual bool d() const = 0;
		virtual bool w() const = 0;
		virtual bool s() const = 0;
		virtual bool j() const = 0;
		virtual bool k() const = 0;
		virtual bool l() const = 0;
		virtual bool u() const = 0;
		virtual bool i() const = 0;
		virtual bool o() const = 0;
	};

	::std::unique_ptr<keyboard>(*default_keyboard)();
};
struct glfw_keyboard : io::keyboard
{
	bool key(int k) const
	{
		auto handle = ::glfwGetCurrentContext();
		if (!handle)
			return false;
		return glfwGetKey(handle, k) == GLFW_PRESS;
	}

	bool a() const override { return key(GLFW_KEY_A); }
	bool d() const override { return key(GLFW_KEY_D); }
	bool w() const override { return key(GLFW_KEY_W); }
	bool s() const override { return key(GLFW_KEY_S); }

	bool j() const override { return key(GLFW_KEY_J); }
	bool k() const override { return key(GLFW_KEY_K); }
	bool l() const override { return key(GLFW_KEY_L); }

	bool u() const override { return key(GLFW_KEY_U); }
	bool i() const override { return key(GLFW_KEY_I); }
	bool o() const override { return key(GLFW_KEY_O); }
};
::std::unique_ptr<io::keyboard> default_keyboard()
{
	return std::make_unique<glfw_keyboard>();
} 

::nagisa::concurrency::simple_task<void> fighter_scene()
{
	io io_module{
		.default_keyboard = ::default_keyboard,
	};
	auto keyboard = io_module.default_keyboard();

	auto renderer = ::default_renderer();

	struct player
	{
		int x, y;
	};
	player a{}, b{};
	constexpr auto w = 50, h = 50;

	auto token = co_await ::stdexec::get_stop_token();
	while (!token.stop_requested())
	{
		if (keyboard->a())
			a.x -= 10;
		if (keyboard->d())
			a.x += 10;
		if (keyboard->w())
			a.y -= 10;
		if (keyboard->s())
			a.y += 10;

		renderer->begin_frame();
		renderer->draw_rect(a.x, a.y, w, h);
		renderer->draw_rect(b.x, b.y, w, h);
		renderer->end_frame();
	}

	co_return;
}


int main()
{
	auto task = ::fighter_scene();
	task.handle().resume();

	return 0;
}