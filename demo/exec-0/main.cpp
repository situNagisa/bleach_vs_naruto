#include <cstdio>
#include <vkkl/command_buffer.h>

import std;
import demo.execution;
import demo.window;
import demo.input;
import demo.render;
import demo.scene.menu;

namespace
{
// 两个访问权由调用方持有，必须活过返回的 sender：record 交出的命令缓冲在这个 sender 内部析构（释放回 menu 的 pool），
// render 内等到 GPU 完成与 present 完成才结束，所以 sender 完成时本帧对两个 entity 变量的访问都已结束。
auto render_frame(renderer const& target, renderer::swapchain_access const& swapchain, menu const& scene, menu::access const& scene_access)
{
	return target.acquire_frame()
		| ::stdexec::let_value([&target, &swapchain, &scene, &scene_access](renderer::frame_pool::permit& lease)
			{
				auto& frame = *lease.token();
				return target.acquire_image(swapchain, frame)
					| ::stdexec::let_value([&target, &swapchain, &scene, &scene_access, &frame](image_acquisition& acquisition)
						{
							return scene.record(scene_access, swapchain.extent())
								| ::stdexec::let_value([&target, &swapchain, &frame, &acquisition](auto& commands, auto&)
									{
										auto handles = commands | ::std::views::transform([](auto const& command) { return command.handle; }) | ::std::ranges::to<::std::vector>();
										return ::stdexec::just(::std::move(handles)) | ::stdexec::let_value([&target, &swapchain, &frame, &acquisition](auto& handles) { return target.render(swapchain, frame, acquisition, handles); });
									});
						});
			});
}
}

int main(int argc, char** argv)
{
	try
	{
		auto font_path = ::std::filesystem::path{};
		for (auto index = 1; index < argc; ++index)
		{
			auto const argument = ::std::string_view{ argv[index] };
			if (argument == "--font" && index + 1 < argc)
				font_path = argv[++index];
			else
				throw ::std::runtime_error{ "usage: exec-0 [--font <TTF/OTF file>]" };
		}
		auto const glfw = glfw_context{};
		auto const main_window = window{ "BVN - Main Menu", 960, 540 };
		auto target = renderer{ main_window };
		// 图形队列需要外部同步：只在持有交换链访问权时取用，菜单构造（上传字体）结束后归还。
		auto scene = [&]
			{
				auto const acquired = ::stdexec::sync_wait(target.acquire_swapchain());
				if (!acquired)
					throw ::std::runtime_error{ "acquire swapchain stopped" };
				return menu{ target.device(), target.physical_device(), ::std::get<0>(*acquired).graphics_queue(), target.graphics_queue_family(), target.format(), font_path };
			}();
		auto keyboard = input{ main_window };
		auto workers = ::exec::static_thread_pool{ 1 };
		auto const render_scheduler = workers.get_scheduler();
		auto resize_pending = false;
		for (;;)
		{
			// 三个起始点：input.update / menu.update / renderer.acquire_swapchain，取得顺序固定以免交叉持有死锁。
			// input 的 permit 在 update 内部写回后即归还；另外两个访问权一直持有到本帧结束。
			auto polled = ::stdexec::sync_wait(keyboard.update());
			if (!polled)
				break;
			auto const [keys] = *::std::move(polled);
			auto updated = ::stdexec::sync_wait(scene.update(keys));
			if (!updated)
				break;
			auto [scene_access] = *::std::move(updated);
			auto const action = scene_access.snapshot().requested;
			if (action == menu::action::quit)
				break;
			if (action == menu::action::start_game)
				::std::println("exec-0: start_game requested (game scene is not installed)");
			auto const extent = main_window.drawable_extent();
			if (extent.width == 0 || extent.height == 0)
			{
				::std::this_thread::sleep_for(::std::chrono::milliseconds{ 16 });
				continue;
			}
			auto acquired = ::stdexec::sync_wait(target.acquire_swapchain());
			if (!acquired)
				break;
			auto [swapchain] = *::std::move(acquired);
			// 修改交换链只能经由访问权；此时没有别人能访问它。resize 要读窗口尺寸，留在主线程。
			auto const current_extent = swapchain.extent();
			if (resize_pending || extent.width != current_extent.width || extent.height != current_extent.height)
			{
				target.resize(swapchain);
				resize_pending = false;
			}
			try
			{
				auto completion = ::std::make_shared<::std::promise<bool>>();
				auto rendered = completion->get_future();
				::exec::start_detached(::stdexec::schedule(render_scheduler)
					| ::stdexec::then([&target, &scene, held_swapchain = ::std::move(swapchain), held_scene = ::std::move(scene_access), completion]() mutable
						{
							try
							{
								// 访问权移进内层作用域：帧 sender 完成（GPU 与 present 都已结束）后先析构归还 permit，
								// 再通知主线程；lambda 本身要等 start_detached 收尾才析构，不能让 permit 留在捕获里。
								auto finished = false;
								{
									auto const frame_swapchain = ::std::move(held_swapchain);
									auto const frame_scene = ::std::move(held_scene);
									finished = ::stdexec::sync_wait(::render_frame(target, frame_swapchain, scene, frame_scene)).has_value();
								}
								completion->set_value(finished);
							}
							catch (...)
							{
								completion->set_exception(::std::current_exception());
							}
						}));
				while (rendered.wait_for(::std::chrono::milliseconds{ 1 }) != ::std::future_status::ready)
					window::poll_events();
				if (!rendered.get())
				break;
			}
			catch (surface_changed const&)
			{
				resize_pending = true;
			}
		}
		return 0;
	}
	catch (::std::exception const& error)
	{
		::std::println(stderr, "exec-0: {}", error.what());
		return 1;
	}
}
