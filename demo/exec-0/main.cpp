
import std;
import demo.execution;
import demo.menu;
import demo.window;

struct application
{
	glfw_context glfw{};
	window main_window{ "exec-0", 800, 600 };
	input keyboard{ main_window };
	::exec::static_thread_pool pool{ 4 };
	::stdexec::run_loop main_thread{};
	::stdexec::counting_scope async_scope{};

	::nagisa::concurrency::simple_task<void> _main()
	{
		auto stop_token = co_await ::stdexec::read_env(::stdexec::get_stop_token);
		while (!stop_token.stop_requested())
		{
			struct input_update;
			struct input_over;
			co_await (
				::stdexec::starts_on(main_thread.get_scheduler(), keyboard.update())
				| ::stdexec::then([&](input::resource_pool_type::permit permit)
					{
						input::state_type const& state = *permit.token();
						if (state.close_requested)
							request_stop();
					})
					);
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
