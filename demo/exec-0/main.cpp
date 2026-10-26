
import std;
import demo.execution;
import demo.menu;
import demo.window;
import demo.render;

// 帧图的节点键。
namespace node
{
struct drawable {}; struct swapchain {}; struct record {}; struct image {}; struct render {};
struct graphics_queue {}; struct submit {}; struct present_queue {}; struct present {};
}

struct application
{
	glfw_context glfw{};
	window main_window{ "exec-0", 800, 600 };
	// blockSize = 1：worker 上提交的任务立即对窃取者可见，否则从 worker 发起的并行分支会挤在同一个线程上。
	::exec::static_thread_pool pool{ 4, ::exec::bwos_params{ .blockSize = 1 } };
	::stdexec::run_loop main_thread{};
	// 专门等 GPU：帧末的 wait_frame 阻塞在 fence 上，放在这里不占 worker。
	::stdexec::run_loop queue_thread{};
	::std::jthread queue_worker{ [this] { queue_thread.run(); } };
	// 先于 queue_worker 析构：让 run 返回，jthread 才能 join（renderer 构造失败等提前退出的路径也一样）。
	struct finish_on_exit
	{
		::stdexec::run_loop& loop;
		~finish_on_exit() { loop.finish(); }
	} queue_finisher{ queue_thread };
	::stdexec::counting_scope async_scope{};
	// 在途的帧。帧池容量限制它们的个数：_main 取到帧 permit 才 spawn 下一帧。
	::stdexec::counting_scope frame_scope{};

	renderer graphics{ main_window };

	using frame_permit = renderer::frame_pool::permit;
	using swapchain_permit = renderer::swapchain_pool::permit;
	using queue_permit = renderer::queue_pool::permit;

	// 一帧的任务图。边只表示真实的数据 / 资源依赖，没有依赖的节点同时在飞：
	//
	//   drawable ──┐
	//   swapchain ─┴─► image ──┬──► render ──► graphics_queue ──► submit ──► present_queue ──► present
	//   record ────────────────┘                                                ▲
	//                image（交换链 permit）─────────────────────────────────────┘
	//
	// - drawable 在主线程读窗口尺寸（GLFW 的约束）；record 录场景的二级命令缓冲（暂时为空），与取图像并行；
	// - image 持有交换链 permit，在其中按需重建交换链并阻塞到取得图像；permit 作为 image 的值 take 给 present，
	//   呈现完 present 节点结束时析构。于是"取图像到呈现"这一段按帧串行（呈现顺序就是取得 permit 的顺序），其余部分跨帧重叠；
	// - 图形队列在录制完成后才取（提前取到只会让别的帧等），提交完换成呈现队列（同族时就是它自己），呈现完析构。
	// 所有 permit 都经 take 传递：sdag 在取走它的节点结束时析构它，资源在用完的那一刻归还，不留到整图结束。
	// 阻塞或耗 CPU 的节点 continues_on(pool)；只调一次 Vulkan 入队的节点就地执行。
	auto frame_graph(renderer::frame_type& frame)
	{
		auto const workers = pool.get_scheduler();
		return ::sdag::dag(
			::sdag::make_node<node::drawable>(
				::stdexec::schedule(main_thread.get_scheduler())
				| ::stdexec::then([this] { return main_window.drawable_extent(); })),
			::sdag::make_node<node::swapchain>(graphics.acquire_swapchain()),
			::sdag::make_node<node::record>(
				::stdexec::schedule(workers)
				| ::stdexec::then([] { return renderer::command_view{}; })),
			// permit 只能 move，continues_on 却要求能拷贝它转发的值：用 let_value 把 permit 留在原地，再换到 worker 上。
			::sdag::make_node<node::image, ::sdag::data<::sdag::take<node::swapchain>, node::drawable>>(
				::stdexec::let_value([this, &frame, workers](swapchain_permit& swapchain, window_extent drawable)
					{
						return ::stdexec::schedule(workers)
							| ::stdexec::then([this, &frame, &swapchain, drawable]
								{
									graphics.acquire_swapchain_image(frame, *swapchain.token(), drawable);
									return ::std::move(swapchain);
								});
					})),
			::sdag::make_node<node::render, ::sdag::data<node::record>, ::sdag::control<node::image>>(
				::stdexec::continues_on(workers)
				| ::stdexec::then([this, &frame](renderer::command_view commands) { graphics.render(frame, commands); })),
			::sdag::make_node<node::graphics_queue, ::sdag::control<node::render>>(graphics.acquire_graphics_queue()),
			::sdag::make_node<node::submit, ::sdag::data<::sdag::take<node::graphics_queue>>>(
				::stdexec::then([this, &frame](queue_permit queue)
					{
						graphics.submit_graphic_queue(queue, frame);
						return queue;
					})),
			::sdag::make_node<node::present_queue, ::sdag::data<::sdag::take<node::submit>>>(
				::stdexec::let_value([this](queue_permit& queue) { return graphics.acquire_present_queue(::std::move(queue)); })),
			::sdag::make_node<node::present, ::sdag::data<::sdag::take<node::image>, ::sdag::take<node::present_queue>>>(
				::stdexec::then([this, &frame](swapchain_permit swapchain, queue_permit queue)
					{ graphics.submit_present_queue(queue, frame, *swapchain.token()); })));
	}

	// 帧 permit 包住整张帧图与帧末等待：finally 保证帧图出错时也先等到 GPU 用完这一帧，permit 才析构归还。
	auto frame_work(frame_permit permit)
	{
		return ::stdexec::let_value(::stdexec::just(::std::move(permit)), [this](frame_permit& permit)
			{
				auto& frame = *permit.token();
				return ::exec::finally(
					frame_graph(frame),
					::stdexec::schedule(queue_thread.get_scheduler())
					| ::stdexec::then([this, &frame] { graphics.wait_frame(frame); }));
			})
			| ::stdexec::upon_error([](::std::exception_ptr) noexcept { ::std::terminate(); });
	}

	::nagisa::concurrency::simple_task<void> _main()
	{
		auto stop_token = co_await ::stdexec::read_env(::stdexec::get_stop_token);
		while (!stop_token.stop_requested())
		{
			// 帧池就是背压：没有空闲的帧时在这里等最早的一帧归还。
			auto frame = co_await graphics.acquire_frame();
			// permit 可能在别的线程上归还，回到主线程再碰 GLFW。
			co_await ::stdexec::schedule(main_thread.get_scheduler());
			window::poll_events();
			if (main_window.should_close())
				break;
			::stdexec::spawn(frame_work(::std::move(frame)), frame_scope.get_token());
		}
		// 在途的帧还要回主线程读窗口尺寸，主线程要跑到它们全部结束。
		co_await ::stdexec::starts_on(main_thread.get_scheduler(), frame_scope.join());
		request_stop();
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
		queue_thread.finish();
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
