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
auto render_frame(renderer const& target, menu const& scene, menu::state snapshot)
{
	return target.acquire_frame()
		| ::stdexec::let_value([&target, &scene, snapshot](renderer::frame_pool::lease& lease)
			{
				auto& frame = *lease.token();
				return target.acquire_image(frame)
					| ::stdexec::let_value([&target, &scene, &frame, snapshot](image_acquisition& acquisition)
						{
							return scene.record(snapshot, target.extent())
								| ::stdexec::let_value([&target, &frame, &acquisition](auto& commands, auto&)
									{
										auto handles = commands | ::std::views::transform([](auto const& command) { return command.handle; }) | ::std::ranges::to<::std::vector>();
										return ::stdexec::just(::std::move(handles)) | ::stdexec::let_value([&target, &frame, &acquisition](auto& handles) { return target.render(frame, acquisition, handles); });
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
		auto scene = menu{ target.device(), target.physical_device(), target.graphics_queue(), target.graphics_queue_family(), target.format(), font_path };
		auto keyboard = input{ main_window };
		auto workers = ::exec::static_thread_pool{ 1 };
		auto const render_scheduler = workers.get_scheduler();
		auto resize_pending = false;
		for (;;)
		{
			auto polled = ::stdexec::sync_wait(keyboard.poll());
			if (!polled)
				break;
			auto const [keys] = *::std::move(polled);
			keyboard.consume(keys);
			auto updated = ::stdexec::sync_wait(scene.update(keys));
			if (!updated)
				break;
			scene.current_state = ::std::get<0>(*updated);
			auto const action = scene.current_state.requested;
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
			auto const current_extent = target.extent();
			if (resize_pending || extent.width != current_extent.width || extent.height != current_extent.height)
			{
				target.resize();
				resize_pending = false;
			}
			try
			{
				auto completion = ::std::make_shared<::std::promise<bool>>();
				auto rendered = completion->get_future();
				::exec::start_detached(::stdexec::schedule(render_scheduler)
					| ::stdexec::then([&target, &scene, state = scene.current_state, completion]
						{
							try
							{
								completion->set_value(::stdexec::sync_wait(::render_frame(target, scene, state)).has_value());
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
