#include <cstdio>

import std;
import demo.execution;
import demo.window;
import demo.input;
import demo.render;
import demo.scene.menu;

namespace
{
// 任务图的节点键。
enum class node
{
	input_update, menu_update,
	frame, image, record, render,
};

// 更新图（主线程：input.update 要泵 GLFW 事件）：input_update -> menu_update，完成值 (menu 的 permit, menu 的变化值)。
// input 的 (permit, 变化值) 被 move 进 menu_update 的 let_value，menu_update 结束时析构，即用完即放。
auto update_graph(input& keyboard, menu& scene)
{
	return ::sdag::dag<node>(
		::sdag::make<::sdag::basic_node{node::input_update}>(keyboard.update()),
		::sdag::make<::sdag::basic_node{node::menu_update, ::sdag::move(node::input_update)}>(::stdexec::let_value([&scene](input::guard_type::permit&, input::change_type& keys) { return scene.update(keys); })));
}

// 帧图（worker）：frame -> image ─┐
//                        record ─┴-> render
// 两个 permit 由调用方持有到 sender 完成之后。frame 租约、图像、命令缓冲都只有一个读者，被 move 进读者的 let_value，
// 在读者结束时析构——render 结束时它已等到 GPU 与 present 完成。
auto frame_graph(renderer const& target, renderer::guard_type::permit const& swapchain, menu const& scene, menu::guard_type::permit const& scene_permit)
{
	return ::sdag::dag<node>(
		::sdag::make<::sdag::basic_node{node::frame}>(target.acquire_frame()),
		::sdag::make<::sdag::basic_node{node::image, ::sdag::move(node::frame)}>(::stdexec::let_value([&target, &swapchain](renderer::frame_pool::permit& lease) { return target.acquire_image(swapchain, *lease.token()); })),
		::sdag::make<::sdag::basic_node{node::record}>(scene.record(scene_permit, target.state().extent)),
		::sdag::make<::sdag::basic_node{node::render, ::sdag::move(node::image), ::sdag::move(node::record)}>(::stdexec::let_value([&target, &swapchain](image_acquisition& acquisition, auto& commands, auto&) { return target.render(swapchain, acquisition, commands); })));
}

// 在 worker 上跑完一帧的渲染图，主线程等待时继续泵窗口事件（present 可能等事件线程）。异常（含 surface_changed）经 future 回到主线程。
// worker 上用 sync_wait 而不是 start_detached(starts_on(...))：要让帧图的 op-state（命令缓冲、frame 租约）先析构，再释放 permit。
bool render_on(auto scheduler, renderer const& target, renderer::guard_type::permit swapchain, menu const& scene, menu::guard_type::permit scene_permit)
{
	auto const task = ::std::make_shared<::std::packaged_task<bool()>>([&]
		{
			// 局部变量逆序析构：帧图 op-state -> menu 的 permit -> renderer 的 permit，之后 future 才就绪。
			auto const held_swapchain = ::std::move(swapchain);
			auto const held_scene = ::std::move(scene_permit);
			return ::stdexec::sync_wait(::frame_graph(target, held_swapchain, scene, held_scene)).has_value();
		});
	auto finished = task->get_future();
	::exec::start_detached(::stdexec::schedule(scheduler) | ::stdexec::then([task] { (*task)(); }));
	while (finished.wait_for(::std::chrono::milliseconds{ 1 }) != ::std::future_status::ready)
		window::poll_events();
	return finished.get();
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
		// 图形队列需要外部同步：菜单构造（上传字体）期间持有交换链的 permit。
		auto scene = [&]
			{
				auto const acquired = ::stdexec::sync_wait(target.acquire_swapchain());
				if (!acquired)
					throw ::std::runtime_error{ "acquire swapchain stopped" };
				return menu{ target.device(), target.physical_device(), target.state().graphics_queue, target.graphics_queue_family(), target.format(), font_path };
			}();
		auto keyboard = input{ main_window };
		auto workers = ::exec::static_thread_pool{ 1 };
		auto resize_pending = false;
		for (;;)
		{
			// 三个起始点按 input -> menu -> renderer 的顺序取得，以免交叉持有死锁。
			auto updated = ::stdexec::sync_wait(::update_graph(keyboard, scene));
			if (!updated)
				break;
			auto [scene_permit, scene_change] = *::std::move(updated);
			if (scene_change.requested == menu::action::quit)
				break;
			if (scene_change.requested == menu::action::start_game)
				::std::println("exec-0: start_game requested (game scene is not installed)");
			auto const extent = main_window.drawable_extent();
			if (extent.width == 0 || extent.height == 0)
			{
				// 最小化：本帧不渲染，scene_permit 在 continue 离开循环体时释放。
				::std::this_thread::sleep_for(::std::chrono::milliseconds{ 16 });
				continue;
			}
			auto acquired = ::stdexec::sync_wait(target.acquire_swapchain());
			if (!acquired)
				break;
			auto [swapchain_permit] = *::std::move(acquired);
			// resize 要读窗口尺寸，留在主线程；持有 permit 时直接读写交换链 state。
			if (resize_pending || extent.width != target.state().extent.width || extent.height != target.state().extent.height)
			{
				target.resize(swapchain_permit);
				resize_pending = false;
			}
			try
			{
				if (!::render_on(workers.get_scheduler(), target, ::std::move(swapchain_permit), scene, ::std::move(scene_permit)))
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
