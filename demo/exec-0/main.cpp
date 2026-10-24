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
// 两个 permit 由调用方持有，必须活过返回的 sender：record 交出的命令缓冲在这个 sender 内部析构（释放回 menu 的 pool），
// render 内等到 GPU 完成与 present 完成才结束，所以 sender 完成时本帧对两个 entity state 的访问都已结束。
auto render_frame(renderer const& target, renderer::guard_type::permit const& swapchain, menu const& scene, menu::guard_type::permit const& scene_permit)
{
	return target.acquire_frame()
		| ::stdexec::let_value([&target, &swapchain, &scene, &scene_permit](renderer::frame_pool::permit& lease)
			{
				auto& frame = *lease.token();
				return target.acquire_image(swapchain, frame)
					| ::stdexec::let_value([&target, &swapchain, &scene, &scene_permit, &frame](image_acquisition& acquisition)
						{
							return scene.record(scene_permit, target.state().extent)
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
				// permit 在 acquired 里，lambda 返回时随它析构。
				return menu{ target.device(), target.physical_device(), target.state().graphics_queue, target.graphics_queue_family(), target.format(), font_path };
			}();
		auto keyboard = input{ main_window };
		auto workers = ::exec::static_thread_pool{ 1 };
		auto const render_scheduler = workers.get_scheduler();
		auto resize_pending = false;
		for (;;)
		{
			// 三个起始点：input.update / menu.update / renderer.acquire_swapchain，取得顺序固定以免交叉持有死锁。
			// 三个 permit 都由这里持有，何时释放由这里决定。
			auto polled = ::stdexec::sync_wait(keyboard.update());
			if (!polled)
				break;
			auto [input_permit, keys] = *::std::move(polled);
			auto updated = ::stdexec::sync_wait(scene.update(keys));
			if (!updated)
				break;
			auto [scene_permit, scene_change] = *::std::move(updated);
			// 释放点 1：menu.update 已用完 input 的变化值，本帧之后没人再读 keyboard.state。
			input_permit = {};
			if (scene_change.requested == menu::action::quit)
				break;
			if (scene_change.requested == menu::action::start_game)
				::std::println("exec-0: start_game requested (game scene is not installed)");
			auto const extent = main_window.drawable_extent();
			if (extent.width == 0 || extent.height == 0)
			{
				// 最小化：本帧不录制、不取交换链，scene_permit 在 continue 离开循环体时析构。
				::std::this_thread::sleep_for(::std::chrono::milliseconds{ 16 });
				continue;
			}
			auto acquired = ::stdexec::sync_wait(target.acquire_swapchain());
			if (!acquired)
				break;
			auto [swapchain_permit] = *::std::move(acquired);
			// 持有 permit 时直接读写交换链 state；此时没有别人能访问它。resize 要读窗口尺寸，留在主线程。
			auto const current_extent = target.state().extent;
			if (resize_pending || extent.width != current_extent.width || extent.height != current_extent.height)
			{
				target.resize(swapchain_permit);
				resize_pending = false;
			}
			try
			{
				auto completion = ::std::make_shared<::std::promise<bool>>();
				auto rendered = completion->get_future();
				::exec::start_detached(::stdexec::schedule(render_scheduler)
					| ::stdexec::then([&target, &scene, held_swapchain = ::std::move(swapchain_permit), held_scene = ::std::move(scene_permit), completion]() mutable
						{
							try
							{
								// permit 移进内层作用域：lambda 本身要等 start_detached 收尾才析构，不能让 permit 留在捕获里。
								auto finished = false;
								{
									auto const frame_swapchain = ::std::move(held_swapchain);
									auto const frame_scene = ::std::move(held_scene);
									finished = ::stdexec::sync_wait(::render_frame(target, frame_swapchain, scene, frame_scene)).has_value();
									// 释放点 2：帧 sender 已完成——GPU 与 present 都已结束，record 交出的命令缓冲已在 sender 内析构；
									// 作用域结束时先析构 frame_scene（menu），再析构 frame_swapchain（renderer），然后才通知主线程。
								}
								completion->set_value(finished);
							}
							catch (...)
							{
								// 异常（含 surface_changed）穿出内层作用域时两个 permit 同样已析构。
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
