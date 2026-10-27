
import std;
import demo.execution;
import demo.menu;
import demo.window;
import demo.render;

// 帧图的节点键。
enum class step
{
	image_slot, access, record, image, render,
	graphics_queue, submit, present_queue, present, wait,
};

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

	renderer graphics{ main_window };

	using frame_permit = renderer::frame_pool::permit;
	using queue_permit = renderer::queue_pool::permit;

	// 一帧的任务图，跑在某一代交换链上（调用方持有它的交换链 permit）。边只表示真实的依赖：
	//
	//   image_slot ──► access ──► image ──┬──► render ──► graphics_queue ──► submit ──► present_queue ──► present ──► wait
	//   record ───────────────────────────┘
	//
	// - image_slot：交换链的图像 permit，持有到 wait 等完 present fence 才析构——这一帧对交换链的使用到此结束。
	//   交换链 permit 等它所有图像 permit 析构后才重建、析构，所以帧里不重建、不碰交换链的生命周期。
	// - access：交换链的外部同步（acquire / present / release），从取图像持有到呈现完：呈现顺序就是取得它的顺序。
	//   在图像 permit 之后才取，等图像时不挡别的帧。
	// - 取图像报过期、呈现报次优 / 过期时标记 stale：持有交换链 permit 的一方不再开新帧，等在途帧结束后重建。
	// - record 录场景的二级命令缓冲（暂时为空），与取图像并行。
	// - 图形队列在录制完成后才取（提前取到只会让别的帧等），提交完换成呈现队列（同族时就是它自己），呈现完析构。
	// - render 出错时图像已经取到却不会呈现：在 render 里把图像还给交换链、等掉 image_available 的触发，再把错误交出去。
	//   submit 之后都是 noexcept，错误只可能发生在 render 及以前。
	// permit 都经 take 传递，sdag 在取走它的节点结束时析构它。
	// 阻塞或耗 CPU 的节点放到 worker / queue_thread 上；只调一次 Vulkan 入队的节点就地执行。
	auto frame_graph(renderer::frame_type& frame, renderer::swapchain_type& swapchain)
	{
		auto const workers = pool.get_scheduler();
		auto const waiter = queue_thread.get_scheduler();
		return ::sdag::dag<step>(
			::sdag::make<::sdag::basic_node{ step::image_slot }>(graphics.acquire_swapchain_image_slot()),
			::sdag::make<::sdag::basic_node{ step::access, ::sdag::wait(step::image_slot) }>(graphics.acquire_swapchain_access()),
			::sdag::make<::sdag::basic_node{ step::record }>(
				::stdexec::schedule(workers)
				| ::stdexec::then([] { return renderer::command_view{}; })),
			::sdag::make<::sdag::basic_node{ step::image, ::sdag::wait(step::access) }>(
				::stdexec::schedule(workers)
				| ::stdexec::then([this, &frame, &swapchain]
					{
						return graphics.acquire_swapchain_image(frame, swapchain);
					})),
			::sdag::make<::sdag::basic_node{ step::render, ::sdag::let(step::image), ::sdag::move(step::record) }>(
				::stdexec::continues_on(workers)
				| ::stdexec::let_value([this, &frame, &swapchain, waiter](renderer::image_index image, renderer::command_view commands)
					{
						return ::stdexec::just()
							| ::stdexec::then([this, &frame, &swapchain, image, commands] { graphics.render(frame, swapchain, image, commands); })
							// access 仍在图里（present 才取走它），归还图像满足外部同步。
							| ::stdexec::let_error([this, &frame, &swapchain, image, waiter](::std::exception_ptr error)
								{
									graphics.release_swapchain_image(swapchain, image);
									return graphics.acquire_graphics_queue()
										| ::stdexec::then([this, &frame](queue_permit queue) noexcept { graphics.drain_frame(queue, frame); })
										| ::stdexec::continues_on(waiter)
										| ::stdexec::then([this, &frame, error]
											{
												graphics.wait_drained(frame);
												::std::rethrow_exception(error);
											});
								});
					})),
			::sdag::make<::sdag::basic_node{ step::graphics_queue, ::sdag::wait(step::render) }>(graphics.acquire_graphics_queue()),
			::sdag::make<::sdag::basic_node{ step::submit, ::sdag::move(step::graphics_queue) }>(
				::stdexec::then([this, &frame](queue_permit queue) noexcept
					{
						graphics.submit_graphic_queue(queue, frame);
						return queue;
					})),
			::sdag::make<::sdag::basic_node{ step::present_queue, ::sdag::move(step::submit) }>(
				::stdexec::let_value([this](queue_permit& queue) { return graphics.acquire_present_queue(::std::move(queue)); })),
			::sdag::make<::sdag::basic_node{ step::present, ::sdag::move(step::access), ::sdag::let(step::image), ::sdag::move(step::present_queue) }>(
				::stdexec::then([this, &frame, &swapchain](renderer::access_permit, renderer::image_index image, queue_permit queue) noexcept
					{ graphics.submit_present_queue(queue, frame, swapchain, image); })),
			::sdag::make<::sdag::basic_node{ step::wait, ::sdag::move(step::image_slot), ::sdag::wait(step::present) }>(
				::stdexec::let_value([this, &frame, waiter](renderer::image_permit&)
					{
						return ::stdexec::schedule(waiter)
							| ::stdexec::then([this, &frame] { graphics.wait_frame(frame); });
					})));
	}

	// 帧 permit 包住整张帧图：图完成时帧的 GPU 工作已经等完（或 render 出错时已经排空），permit 才析构归还。
	// 出错的帧丢掉，不影响后面的帧。
	auto frame_work(frame_permit permit, renderer::swapchain_type& swapchain)
	{
		return ::stdexec::let_value(::stdexec::just(::std::move(permit)), [this, &swapchain](frame_permit& permit)
			{
				return frame_graph(*permit.token(), swapchain);
			})
			| ::stdexec::upon_error([](::std::exception_ptr error) noexcept
				{
					try { ::std::rethrow_exception(error); }
					catch (surface_changed const&) {}
					catch (::std::exception const& exception) { ::std::println("frame dropped: {}", exception.what()); }
					catch (...) { ::std::println("frame dropped"); }
				});
	}

	::nagisa::concurrency::simple_task<void> _main()
	{
		auto stop_token = co_await ::stdexec::read_env(::stdexec::get_stop_token);
		auto quit = false;
		while (!quit && !stop_token.stop_requested())
		{
			// 一代交换链一个作用域：在它上面跑帧，帧全部结束（所有图像 permit 析构）后重建，最后析构交换链 permit。
			auto swapchain_permit = co_await graphics.acquire_swapchain();
			auto& swapchain = *swapchain_permit.token();
			auto frames = ::stdexec::counting_scope{};
			for (;;)
			{
				// 帧池就是背压：没有空闲的帧时在这里等最早的一帧归还。
				auto frame = co_await graphics.acquire_frame();
				// permit 可能在别的线程上归还，回到主线程再碰 GLFW。
				co_await ::stdexec::schedule(main_thread.get_scheduler());
				window::poll_events();
				if (main_window.should_close() || stop_token.stop_requested())
				{
					quit = true;
					break;
				}
				// 最小化时不渲染，等到有窗口事件再看；帧 permit 在 continue 时析构。
				auto const drawable = main_window.drawable_extent();
				if (drawable.width == 0 || drawable.height == 0)
				{
					window::wait_events();
					continue;
				}
				// stale 由取图像 / 呈现在 access 下写入，这里同样在 access 下读。
				auto const stale = co_await (graphics.acquire_swapchain_access()
					| ::stdexec::then([&swapchain](renderer::access_permit) noexcept { return swapchain.stale; }));
				if (stale || !renderer::fits(swapchain, drawable))
					break;
				::stdexec::spawn(frame_work(::std::move(frame), swapchain), frames.get_token());
			}
			co_await ::stdexec::starts_on(main_thread.get_scheduler(), frames.join());
			if (quit)
				break;
			// 重建要非零尺寸：join 期间可能被最小化，等到窗口恢复。
			auto drawable = main_window.drawable_extent();
			while ((drawable.width == 0 || drawable.height == 0) && !main_window.should_close())
			{
				window::wait_events();
				drawable = main_window.drawable_extent();
			}
			if (main_window.should_close())
				break;
			graphics.rebuild_swapchain(swapchain, drawable);
		}
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
