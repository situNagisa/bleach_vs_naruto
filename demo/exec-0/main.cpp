
import std;
import demo.execution;
import demo.menu;
import demo.window;
import demo.render;

struct application
{
	glfw_context glfw{};
	window main_window{ "exec-0", 800, 600 };
	::exec::static_thread_pool pool{ 4 };
	::stdexec::run_loop main_thread{};
	::stdexec::counting_scope async_scope{};

	renderer graphics{ main_window };

	::nagisa::concurrency::simple_task<void> _main()
	{
		auto stop_token = co_await ::stdexec::read_env(::stdexec::get_stop_token);
		while (!stop_token.stop_requested())
		{
			// 两份资源互不依赖，同时获取；租约一直保留到本帧结束。
			auto [frame, swapchain] = co_await ::stdexec::when_all(graphics.acquire_frame(), graphics.acquire_swapchain());
			auto needs_resize = false;
			try
			{
				co_await ::stdexec::schedule(pool.get_scheduler());
				auto image_index = co_await graphics.acquire_swapchain_image(*frame.token(), *swapchain.token());

				// 录制依赖图像；获取 graphics 队列与录制没有依赖，可以并发。
				// 空 commands 暂时只清屏，后续在这里接入场景的二级命令缓冲。
				auto [graphics_queue] = co_await ::stdexec::when_all(
					::stdexec::starts_on(pool.get_scheduler(), graphics.render(*frame.token(), *swapchain.token(), image_index, {})),
					graphics.acquire_graphics_queue());

				// when_all 完成才允许提交：同时满足“录制完成”和“取得队列”。
				co_await graphics.submit_graphic_queue(graphics_queue, *frame.token());
				graphics_queue = {};

				// 图形提交完成并归还队列后再获取 present，同 family 单队列也不会自锁。
				auto present_queue = co_await graphics.acquire_present_queue();
				co_await graphics.submit_present_queue(present_queue, *frame.token(), *swapchain.token(), image_index);
			}
			catch (surface_changed const&)
			{
				needs_resize = true;
			}

			// GLFW 与交换链重建回到主线程；停止请求只在这一帧收尾后发出。
			co_await ::stdexec::schedule(main_thread.get_scheduler());
			window::poll_events();
			if (main_window.should_close())
				request_stop();
			else if (needs_resize)
				graphics.resize(*swapchain.token());
		}
	}

	void run()
	{
		::stdexec::spawn(
			::stdexec::starts_on(main_thread.get_scheduler(), _main())
			| ::stdexec::upon_error([](::std::exception_ptr) noexcept { ::std::terminate(); })
			, async_scope.get_token());
	}

	void request_stop()
	{
		async_scope.request_stop();
		pool.request_stop();
		main_thread.finish();
	}

	~application()
	{
		request_stop();
		::stdexec::sync_wait(async_scope.join());
	}
};

int main(int argc, char** argv)
try
{
	application app{};
	app.run();
	app.main_thread.run();

	return 0;
}
catch (::std::exception const& error)
{
	::std::println("error: {}", error.what());
	return 1;
}
