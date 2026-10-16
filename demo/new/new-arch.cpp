#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <print>
#include <memory>
#include <optional>
#include <variant>
#include <chrono>
#include <thread>
#include <tuple>

#include <boost/sml.hpp>

#include <SDL3/SDL_init.h>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/variant_sender.hpp>

#include <nagisa/concurrency/when_all_range.h>
#include <nagisa/concurrency/coroutine.h>
#include <nagisa/concurrency/simple_task.h>
#include <nagisa/concurrency/_manual_lifetime.h>

#include <bvn/platform/sdl_context.h>

#include "./entity/render.h"
#include "./entity/input.h"
#include "./entity/main_menu.h"
#include "./entity/setting_scene.h"
#include "./entity/fighting_scene.h"
#include "./entity/fighting_scene/module.h"
#include "./framework/any_sender.h"
#include "./app_state.h"
#include "./entity/frame_clock.h"
#include "./entity/setting_scene/settings.h"

int main() try
{
	auto sdl = ::bvn::platform::sdl_context{};
	auto pool = ::exec::static_thread_pool{ 4 };
	auto stop_source = ::stdexec::inplace_stop_source{};

	struct exit_scene {};

	struct entitites_t
	{
		// 全局资源，先声明后析构：场景和场景里的 entity 都引用它们。
		any_scheduler_type workers;
		module_cache modules{};
		renderer draw{};
		input controls{ draw.window };
		union scene_t
		{
			exit_scene exit;
			main_menu menu;
			fighting_scene fighting;

			~scene_t() noexcept {}
		} scene{};
		setting_scene settings{draw};
		frame_clock clock{};
	} entitites{ .workers = pool.get_scheduler() };

	// main 规定、main 持有。场景每帧拿到自己那一块的引用去改，main 在帧结束后读。
	auto app = app_state{ .settings = { .preferences = ::load_settings(setting_path) } };
	entitites.draw.set_fullscreen(app.settings.preferences.fullscreen);

	struct exit {};
	struct open_settings {};
	struct start_fighting {};
	struct back_to_menu {};

	struct machine
	{
		auto operator()() const noexcept{
			using ::boost::sml::state;
			using ::boost::sml::event;
			using ::boost::sml::on_entry;
			using ::boost::sml::on_exit;
			using ::boost::sml::_;
			return ::boost::sml::make_transition_table(
				*state<main_menu> + on_entry<_> / [] (entitites_t& entitites) { ::std::construct_at(&entitites.scene.menu, entitites.draw); }
				, state<main_menu> + on_exit<_> / [] (entitites_t& entitites) noexcept { ::std::destroy_at(&entitites.scene.menu); }
				, state<main_menu> + event<exit> = state<exit_scene>
				, state<main_menu> + event<open_settings> = state<setting_scene>
				, state<main_menu> + event<start_fighting> = state<fighting_scene>

				, state<setting_scene> + event<back_to_menu> = state<main_menu>
				, state<setting_scene> + event<exit> = state<exit_scene>

				, state<fighting_scene> + on_entry<_> / [] (entitites_t& entitites) { ::std::construct_at(&entitites.scene.fighting, entitites.controls, entitites.draw, entitites.modules, entitites.workers); }
				, state<fighting_scene> + on_exit<_> / [] (entitites_t& entitites) noexcept { ::std::destroy_at(&entitites.scene.fighting); }
				, state<fighting_scene> + event<back_to_menu> = state<main_menu>
				, state<fighting_scene> + event<exit> = state<exit_scene>
				);
		}
	};

	::boost::sml::sm<machine> sm{ entitites };

	/// 帧末推进状态机，按场景分派。返回 false 表示退出主循环。
	struct transition
	{
		::boost::sml::sm<machine>& sm;
		entitites_t& entitites;
		app_state& app;

		auto operator()(main_menu&) const -> bool
		{
			if (app.menu.activated == 0)
				sm.process_event(start_fighting{});
			else if (app.menu.activated == 1)
				sm.process_event(open_settings{});
			else if (app.menu.activated == 2)
			{
				sm.process_event(exit{});
				return false;
			}
			return true;
		}

		auto operator()(setting_scene&) const -> bool
		{
			entitites.draw.set_fullscreen(app.settings.preferences.fullscreen);
			if (app.settings.back_requested)
			{
				::save_settings(setting_path, app.settings.preferences);
				sm.process_event(back_to_menu{});
			}
			return true;
		}

		auto operator()(fighting_scene& scene) const -> bool
		{
			// spawn / despawn：fence 已等过，本帧的并发阶段全部结束。
			scene.apply_structural_changes();
			if (app.fighting.back_requested)
				sm.process_event(back_to_menu{});
			return true;
		}
	};

	// 一帧：
	//   [collect 模块]
	//   renderer.begin ∥ ((tick ∥ poll) → update)
	//   → record（可能跑在线程池）→ 切回主线程 → renderer.end（submit/present/等 fence）
	//   → 推进状态机。完成值：是否继续。
	//
	// `shared` 是 main 交给场景的那一块 app_state。并发是安全的：同一帧里只有这个场景的 update
	// 写它，读它的 transition 排在 end 之后；tick / poll / begin 碰的是各自 entity 的私有状态。
	//
	// let_value 的回调参数**必须是引用**：let_value 把上游的值存在自己的 operation_state 里，
	// 一直活到内层 sender 完成。recording 持有命令缓冲和顶点缓冲，要活到 end 里的 fence 等完。
	//
	// 主线程：sync_wait 的 env 里 get_scheduler 就是它驱动的 run_loop，也就是主线程。
	// poll 内联在主线程上开始；record 可能在 worker 上完成，所以 end 之前切回来——
	// present 和 transition（SDL、set_fullscreen、场景析构）都要在主线程。
	//
	// transition 必须是最后一步：
	// - fence 已经等过，set_fullscreen 重建交换链、despawn 释放 entity 的绘制资源都安全；
	// - on_exit 会析构当前场景，而本帧的 operation_state 此时还活着（sync_wait 返回后才销毁）。
	//   之后只剩完成信号和析构，不会再碰场景对象；模块里的代码由 module_cache 留到下一帧开头才卸载。
	auto const scene_frame = [&](auto& scene, auto& shared)
	{
		return ::stdexec::read_env(::stdexec::get_scheduler)
			| ::stdexec::let_value([&](auto main_thread)
				{
					// 上一帧的 sync_wait 已经返回，它的 operation_state 全部析构：没人用的模块现在可以卸了。
					entitites.modules.collect();

					return ::stdexec::when_all(
						entitites.draw.begin()
						, ::stdexec::when_all(entitites.clock.tick(), entitites.controls.poll())
							| ::stdexec::let_value([&](frame_clock::frame_time const& time, input::state const& current, input::delta const& changes)
								{
									return scene.update(shared, time, current, changes);
								})
						)
						| ::stdexec::let_value([&, main_thread](renderer::lease& slot, auto& snapshot, auto&...)
							{
								return
									scene.record(*slot.token(), snapshot)
									| ::stdexec::continues_on(main_thread)
									| ::stdexec::let_value([&](auto& recording)
										{
											return entitites.draw.end(*slot.token(), recording.commands());
										});
							})
						| ::stdexec::then([&]
							{
								assert(::SDL_IsMainThread());
								if (entitites.controls._current.quit_requested)
								{
									sm.process_event(exit{});
									return false;
								}
								return transition{ sm, entitites, app }(scene);
							});
				});
	};

	// 按当前状态挑这一帧跑哪个场景。variant_sender 的 operation_state 只构造选中的那一支。
	using frame_sender = ::exec::variant_sender<
		decltype(scene_frame(entitites.scene.menu, app.menu)),
		decltype(scene_frame(entitites.settings, app.settings)),
		decltype(scene_frame(entitites.scene.fighting, app.fighting))>;

	auto const frame = [&]() -> frame_sender
	{
		if (sm.is(::boost::sml::state<main_menu>))
			return scene_frame(entitites.scene.menu, app.menu);
		if (sm.is(::boost::sml::state<setting_scene>))
			return scene_frame(entitites.settings, app.settings);
		return scene_frame(entitites.scene.fighting, app.fighting);
	};

	for (auto running = true; running;)
	{
		auto result = ::stdexec::sync_wait(frame());
		running = result && ::std::get<0>(*result);   // 被取消（nullopt）也退出
	}
	::std::println("Rendered {} frames.", entitites.clock.index());
}
catch (::std::exception const& error)
{
	::std::println(stderr, "{}", error.what());
	return 1;
}
