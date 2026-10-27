
import std;
import demo.execution;
import demo.menu;
import demo.window;
import demo.render;

// 帧图的节点键。
enum class step
{
	swapchain, record, image, render,
	graphics_queue, submit, present_queue, present,
	wait, extent, release_swapchain,
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
	// 在途的帧。帧池容量限制它们的个数：_main 取到帧 permit 才 spawn 下一帧。
	::stdexec::counting_scope frame_scope{};

	renderer graphics{ main_window };

	using frame_permit = renderer::frame_pool::permit;
	using swapchain_permit = renderer::swapchain_pool::permit;
	using queue_permit = renderer::queue_pool::permit;

	// 一帧的任务图。边只表示真实的数据 / 资源依赖，没有依赖的节点同时在飞：
	//
	//   swapchain ──► image ──┬──► render ──► graphics_queue ──► submit ──► present_queue ──► present ──► wait ──┬──► release_swapchain
	//   record ───────────────┘                                                                      extent ◄──┘
	//
	// - 交换链 permit 从 swapchain 一直留到 release_swapchain：期间交换链与它的图像都归这一帧。
	//   销毁交换链（重建）是 permit 的最后一件工作，只在 permit 的最后一个持有节点里做：
	//   正常路径是 release_swapchain（wait 之后，extent 读到的是此刻的窗口尺寸）；
	//   取图像就过期时本帧到此为止，image 是最后一个持有节点，在它的错误路径里重建再丢帧。
	// - record 录场景的二级命令缓冲（暂时为空），与取图像并行。
	// - 图形队列在录制完成后才取（提前取到只会让别的帧等），提交完换成呈现队列（同族时就是它自己），呈现完析构。
	// - render 出错时图像已经取到却不会呈现：在 render 里把图像还给交换链、等掉 image_available 的触发，再把错误交出去。
	//   submit 之后都是 noexcept，错误只可能发生在 render 及以前。
	// permit 都经 take 传递，sdag 在取走它的节点结束时析构它。
	// 阻塞或耗 CPU 的节点放到 worker / queue_thread 上；GLFW 只在主线程；只调一次 Vulkan 入队的节点就地执行。
	auto frame_graph(renderer::frame_type& frame)
	{
		auto const workers = pool.get_scheduler();
		auto const waiter = queue_thread.get_scheduler();
		auto const main = main_thread.get_scheduler();
		return ::sdag::dag(
			::sdag::make_node<step::swapchain>(graphics.acquire_swapchain()),
			::sdag::make_node<step::record>(
				::stdexec::schedule(workers)
				| ::stdexec::then([] { return renderer::command_view{}; })),
			// permit 只能 move，换线程却要拷贝转发的值：permit 留在图里（release_swapchain 才 take 它），
			// 节点从它借出交换链指针再换线程，借用期间 permit 一定还活着。
			::sdag::make_node<step::image, ::sdag::data<step::swapchain>>(
				::stdexec::then([](swapchain_permit const& swapchain) noexcept { return swapchain.token(); })
				| ::stdexec::continues_on(workers)
				| ::stdexec::let_value([this, &frame, workers, main](renderer::swapchain_type* swapchain)
					{
						return ::stdexec::just()
							| ::stdexec::then([this, &frame, swapchain] { return graphics.acquire_swapchain_image(frame, *swapchain); })
							| ::stdexec::let_error([this, swapchain, workers, main](::std::exception_ptr error)
								{
									return ::stdexec::schedule(main)
										| ::stdexec::then([this] { return main_window.drawable_extent(); })
										| ::stdexec::continues_on(workers)
										| ::stdexec::then([this, swapchain, error](window_extent drawable) -> renderer::image_index
											{
												try { ::std::rethrow_exception(error); }
												catch (surface_changed const&)
												{
													if (renderer::needs_rebuild(*swapchain, drawable, true))
														graphics.rebuild_swapchain(*swapchain, drawable);
												}
												catch (...) {}
												::std::rethrow_exception(error);
											});
								});
					})),
			::sdag::make_node<step::render, ::sdag::data<step::swapchain, step::image, ::sdag::take{ step::record }>>(
				::stdexec::then([](swapchain_permit const& swapchain, renderer::image_index image, renderer::command_view commands) noexcept
					{ return ::std::tuple{ swapchain.token(), image, commands }; })
				| ::stdexec::continues_on(workers)
				| ::stdexec::let_value([this, &frame, waiter](::std::tuple<renderer::swapchain_type*, renderer::image_index, renderer::command_view> arguments)
					{
						auto const [swapchain, image, commands] = arguments;
						return ::stdexec::just()
							| ::stdexec::then([this, &frame, swapchain, image, commands] { graphics.render(frame, *swapchain, image, commands); })
							| ::stdexec::let_error([this, &frame, swapchain, image, waiter](::std::exception_ptr error)
								{
									graphics.release_swapchain_image(*swapchain, image);
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
			::sdag::make_node<step::graphics_queue, ::sdag::control<step::render>>(graphics.acquire_graphics_queue()),
			::sdag::make_node<step::submit, ::sdag::data<::sdag::take{ step::graphics_queue }>>(
				::stdexec::then([this, &frame](queue_permit queue) noexcept
					{
						graphics.submit_graphic_queue(queue, frame);
						return queue;
					})),
			::sdag::make_node<step::present_queue, ::sdag::data<::sdag::take{ step::submit }>>(
				::stdexec::let_value([this](queue_permit& queue) { return graphics.acquire_present_queue(::std::move(queue)); })),
			::sdag::make_node<step::present, ::sdag::data<step::swapchain, step::image, ::sdag::take{ step::present_queue }>>(
				::stdexec::then([this, &frame](swapchain_permit const& swapchain, renderer::image_index image, queue_permit queue)
					{ return graphics.submit_present_queue(queue, frame, *swapchain.token(), image); })),
			::sdag::make_node<step::wait, ::sdag::data<::sdag::take{ step::present }>>(
				::stdexec::continues_on(waiter)
				| ::stdexec::then([this, &frame](bool stale) noexcept
					{
						graphics.wait_frame(frame);
						return stale;
					})),
			::sdag::make_node<step::extent, ::sdag::control<step::wait>>(
				::stdexec::schedule(main)
				| ::stdexec::then([this] { return main_window.drawable_extent(); })),
			::sdag::make_node<step::release_swapchain, ::sdag::data<::sdag::take{ step::swapchain }, ::sdag::take{ step::wait }, ::sdag::take{ step::extent }>>(
				::stdexec::let_value([this, workers](swapchain_permit& swapchain, bool stale, window_extent drawable)
					{
						return ::stdexec::schedule(workers)
							| ::stdexec::then([this, &swapchain, stale, drawable]
								{
									if (renderer::needs_rebuild(*swapchain.token(), drawable, stale))
										graphics.rebuild_swapchain(*swapchain.token(), drawable);
								});
					})));
	}

	// 帧 permit 包住整张帧图：图完成时帧的 GPU 工作已经等完（或 render 出错时已经排空），permit 才析构归还。
	// 出错的帧丢掉，不影响后面的帧。
	auto frame_work(frame_permit permit)
	{
		return ::stdexec::let_value(::stdexec::just(::std::move(permit)), [this](frame_permit& permit)
			{
				return frame_graph(*permit.token());
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
		while (!stop_token.stop_requested())
		{
			// 帧池就是背压：没有空闲的帧时在这里等最早的一帧归还。
			auto frame = co_await graphics.acquire_frame();
			// permit 可能在别的线程上归还，回到主线程再碰 GLFW。
			co_await ::stdexec::schedule(main_thread.get_scheduler());
			window::poll_events();
			if (main_window.should_close())
				break;
			// 最小化时不渲染：帧 permit 在这里析构，帧图里不需要这条分支。
			auto const drawable = main_window.drawable_extent();
			if (drawable.width == 0 || drawable.height == 0)
				continue;
			::stdexec::spawn(frame_work(::std::move(frame)), frame_scope.get_token());
		}
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
