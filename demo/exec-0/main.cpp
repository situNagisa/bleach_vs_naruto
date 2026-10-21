// main.cpp 是普通翻译单元：除了 stderr 宏需要的 <cstdio>，其余全部通过 import 获得。
// 这里不会解析 stdexec / vkfu / vkkl / Vulkan / GLFW 的任何头文件。
#include <cstdio>

import std;
import demo.execution;
import demo.window;
import demo.input;
import demo.render;

namespace
{
// 一帧的渲染流水线：租一个 frame -> 取 swapchain 图像 -> 录制 / 提交 / 呈现。
// 返回的是 sender，由调用方决定在哪个 scheduler 上启动。
auto render_frame(renderer const& target)
{
	return target.acquire_frame()
		| ::stdexec::let_value([&target](renderer::frame_pool::lease& lease)
			{
				auto& frame = *lease.token();
				return target.acquire_image(frame)
					| ::stdexec::let_value([&target, &frame](image_acquisition& acquisition)
						{
							return target.render(frame, acquisition, {});
						});
			});
}

constexpr auto key_names = ::std::array{
	"up", "down", "left", "right", "a", "b", "c", "d", "e", "f", "g",
};
static_assert(key_names.size() == ::std::to_underlying(key_code::_max));
}

int main()
{
	try
	{
		auto const glfw = glfw_context{};
		auto const main_window = window{ "bvn exec-0", 960, 540 };
		auto const target = renderer{ main_window };
		auto keyboard = input{ main_window };

		// 输入必须在主线程轮询（GLFW 约束）；渲染放到独立的工作线程上。
		auto workers = ::exec::static_thread_pool{ 1 };
		auto const render_scheduler = workers.get_scheduler();

		for (;;)
		{
			auto polled = ::stdexec::sync_wait(keyboard.poll());
			if (!polled)
				break;
			auto const [state] = *::std::move(polled);
			keyboard.consume(state);
			if (state._quit)
				break;

			for (auto index = ::std::size_t{}; index < key_names.size(); ++index)
			{
				if (state._status[index].signal)
					::std::println("exec-0: key {} pressed", key_names[index]);
			}

			if (!::stdexec::sync_wait(::stdexec::starts_on(render_scheduler, render_frame(target))))
				break;
		}
		return 0;
	}
	catch (::std::exception const& error)
	{
		::std::println(stderr, "exec-0: {}", error.what());
		return 1;
	}
}
