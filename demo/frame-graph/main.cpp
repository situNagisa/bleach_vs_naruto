/// frame-graph demo。
///
/// 八个场景，各自钉住设计里的一条结论：
///
///   1. 注册顺序无关   —— renderer 排在名单第一个还是最后一个，结果完全一致。
///   2. 条件性录制     —— 本帧被剔除的 entity 根本不注册，render.end 照常提交。
///   3. 空名单         —— 一个录制者都没有时，begin 直连 end，`when_all_range` 立即完成。
///   4. 录制失败       —— 异常传到 main，**fence 仍然执行**，slot 不会留在占用态。
///   5. 整帧取消       —— 上层 request_stop，**fence 仍然执行**。
///   6. 迟到注册       —— 名单封存后再注册是响亮的异常，不是静默丢失。
///   7. 帧间隔离       —— 连续多帧，每帧一套全新 job，slot 按环轮转。
///   8. 动态名单       —— 帧与帧之间增删 entity 立刻生效，依赖边仍是具体类型直连。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "./entities.h"
#include "./frame_graph.h"
#include "./renderer.h"

namespace fg = ::bvn::frame_graph;
namespace demo = ::bvn::frame_graph::demo;

namespace
{

::std::size_t g_failures = 0;

auto check(bool condition, ::std::string_view what) -> void
{
	::std::println("  [{}] {}", condition ? "ok  " : "FAIL", what);
	if (!condition)
	{
		++g_failures;
	}
}

auto section(::std::string_view title) -> void
{
	::std::println("");
	::std::println("{}", title);
}

[[nodiscard]] auto count_in_frame(
	::std::vector<demo::event> const& events,
	demo::event_kind kind,
	::std::uint64_t frame
) -> ::std::size_t
{
	auto total = ::std::size_t{0};
	for (auto&& entry : events)
	{
		if (entry._kind == kind && entry._frame == frame)
		{
			++total;
		}
	}

	return total;
}

/// 事件流里 begin / record / submit / fence 的相对次序是否正确。
[[nodiscard]] auto ordering_is_sane(demo::render_log const& log) -> bool
{
	auto const events = log.snapshot();
	auto begin_at = events.size();
	auto submit_at = events.size();
	auto fence_at = events.size();
	auto first_record = events.size();
	auto last_record = ::std::size_t{0};
	auto saw_record = false;

	for (auto index = ::std::size_t{0}; index != events.size(); ++index)
	{
		using enum demo::event_kind;
		switch (events[index]._kind)
		{
		case begin:  begin_at = index; break;
		case submit: submit_at = index; break;
		case fence:  fence_at = index; break;
		case record:
			saw_record = true;
			first_record = ::std::min(first_record, index);
			last_record = ::std::max(last_record, index);
			break;
		case compute: break;
		}
	}

	if (begin_at == events.size() || submit_at == events.size() || fence_at == events.size())
	{
		return false;
	}

	if (saw_record && !(begin_at < first_record && last_record < submit_at))
	{
		return false;
	}

	return submit_at < fence_at;
}

// ------------------------------------------------------- 1. 注册顺序无关

/// 同一批 entity，只换 `world::add` 的顺序，跑一帧，返回录制者名字（已排序）。
[[nodiscard]] auto run_with_order(bool renderer_first) -> ::std::vector<::std::string>
{
	auto pool = ::exec::static_thread_pool{4};
	auto log = demo::render_log{};
	auto render = demo::renderer{log};
	auto ground = demo::terrain{render, log, "terrain"};
	auto hud = demo::overlay{render, log, "hud"};
	auto sim = demo::physics{log, "physics"};

	auto stage = fg::world{pool};
	if (renderer_first)
	{
		stage.add(render);
		stage.add(ground);
		stage.add(hud);
		stage.add(sim);
	}
	else
	{
		stage.add(sim);
		stage.add(ground);
		stage.add(hud);
		stage.add(render);
	}

	{
		auto current = fg::frame{stage};
		static_cast<void>(current.run());
	}

	auto names = ::std::vector<::std::string>{};
	for (auto&& entry : log.snapshot())
	{
		if (entry._kind == demo::event_kind::record)
		{
			names.push_back(entry._who);
		}
	}

	::std::ranges::sort(names);
	return names;
}

auto scenario_registration_order() -> void
{
	section("1. 注册顺序无关（renderer 排第一 vs 排最后）");

	auto const first = run_with_order(true);
	auto const last = run_with_order(false);

	check(first.size() == 2, ::std::format("renderer 在前：录到 {} 个（期望 2）", first.size()));
	check(last.size() == 2, ::std::format("renderer 在后：录到 {} 个（期望 2）", last.size()));
	check(first == last, "两种顺序录制者集合一致");
}

// ------------------------------------------------------- 2. 条件性录制

auto scenario_conditional_recording() -> void
{
	section("2. 条件性录制（terrain 本帧被剔除）");

	auto pool = ::exec::static_thread_pool{4};
	auto log = demo::render_log{};
	auto render = demo::renderer{log};
	auto ground = demo::terrain{render, log, "terrain"};
	auto hud = demo::overlay{render, log, "hud"};

	ground._visible = false;

	auto stage = fg::world{pool};
	stage.add(render);
	stage.add(ground);
	stage.add(hud);

	{
		auto current = fg::frame{stage};
		check(current.run(), "整帧正常完成");
	}

	check(!log.has(demo::event_kind::record, "terrain"), "terrain 没有录制");
	check(log.has(demo::event_kind::record, "hud"), "hud 录制了");
	check(!log.has(demo::event_kind::compute, "terrain"), "terrain 的计算节点根本没被构建");
	check(log.count(demo::event_kind::submit) == 1, "render.end 照常提交");
	check(ground._released == 1, "terrain 的 fence 后清理仍然执行");
	check(!render.any_slot_busy(), "没有 slot 停在占用态");
}

// ----------------------------------------------------------- 3. 空名单

auto scenario_empty_roster() -> void
{
	section("3. 空名单（一个录制者都没有）");

	auto pool = ::exec::static_thread_pool{4};
	auto log = demo::render_log{};
	auto render = demo::renderer{log};
	auto sim = demo::physics{log, "physics"};

	auto stage = fg::world{pool};
	stage.add(render);
	stage.add(sim);

	{
		auto current = fg::frame{stage};
		check(current.run(), "整帧正常完成");
	}

	check(log.count(demo::event_kind::record) == 0, "零条录制");
	check(log.count(demo::event_kind::submit) == 1, "begin 直连 end，照常提交");
	check(log.count(demo::event_kind::fence) == 1, "fence 执行了一次");
	check(sim._steps == 1, "纯计算 entity 照常跑（它跟 render 毫无关系）");
	check(ordering_is_sane(log), "begin < submit < fence");
}

// --------------------------------------------------------- 4. 录制失败

auto scenario_record_failure() -> void
{
	section("4. 录制失败（一个录制者抛异常）");

	auto pool = ::exec::static_thread_pool{4};
	auto log = demo::render_log{};
	auto render = demo::renderer{log};
	auto bad = demo::overlay{render, log, "bad"};
	auto slow = demo::overlay{render, log, "slow"};
	auto ground = demo::terrain{render, log, "terrain"};

	bad._fail = true;
	slow._record_delay = ::std::chrono::milliseconds{30};

	auto stage = fg::world{pool};
	stage.add(render);
	stage.add(bad);
	stage.add(slow);
	stage.add(ground);

	auto caught = ::std::string{};
	{
		auto current = fg::frame{stage};
		try
		{
			static_cast<void>(current.run());
		}
		catch (::std::exception const& error)
		{
			caught = error.what();
		}
	}

	check(caught.find("录制失败") != ::std::string::npos, ::std::format("异常传到 main：{}", caught));
	check(log.count(demo::event_kind::submit) == 0, "提交被跳过");
	check(log.count(demo::event_kind::fence) == 1, "fence 仍然执行（错误路径）");
	check(!render.any_slot_busy(), "没有 slot 停在占用态");
	check(ground._released == 0, "下游的清理没有在失败时被误跑");
}

// --------------------------------------------------------- 5. 整帧取消

auto scenario_external_cancel() -> void
{
	section("5. 整帧取消（上层 request_stop）");

	auto pool = ::exec::static_thread_pool{4};
	auto log = demo::render_log{};
	auto render = demo::renderer{log};
	auto hud = demo::overlay{render, log, "hud"};
	auto ground = demo::terrain{render, log, "terrain"};

	auto stage = fg::world{pool};
	stage.add(render);
	stage.add(hud);
	stage.add(ground);

	auto source = ::stdexec::inplace_stop_source{};
	source.request_stop();

	auto completed = true;
	{
		auto current = fg::frame{stage};
		completed = current.run(source.get_token());
	}

	check(!completed, "整帧报告为已取消");
	check(log.count(demo::event_kind::record) == 0, "没有录制发生");
	check(log.count(demo::event_kind::submit) == 0, "没有提交");
	check(log.count(demo::event_kind::fence) == 1, "fence 仍然执行（取消路径）");
	check(!render.any_slot_busy(), "没有 slot 停在占用态");
}

// --------------------------------------------------------- 6. 迟到注册

auto scenario_late_registration() -> void
{
	section("6. 迟到注册（名单封存后再挂录制节点）");

	auto pool = ::exec::static_thread_pool{4};
	auto log = demo::render_log{};
	auto render = demo::renderer{log};
	auto hud = demo::overlay{render, log, "hud"};

	hud._late_register = true;

	auto stage = fg::world{pool};
	stage.add(render);
	stage.add(hud);

	auto caught = ::std::string{};
	{
		auto current = fg::frame{stage};
		try
		{
			static_cast<void>(current.run());
		}
		catch (::std::exception const& error)
		{
			caught = error.what();
		}
	}

	check(caught.find("封存") != ::std::string::npos, ::std::format("迟到注册是响亮的异常：{}", caught));
	check(log.count(demo::event_kind::fence) == 1, "fence 仍然执行");
	check(!render.any_slot_busy(), "没有 slot 停在占用态");
}

// --------------------------------------------------------- 7. 帧间隔离

auto scenario_frame_isolation() -> void
{
	section("7. 帧间隔离（连续 5 帧）");

	auto pool = ::exec::static_thread_pool{4};
	auto log = demo::render_log{};
	auto render = demo::renderer{log};
	auto ground = demo::terrain{render, log, "terrain"};
	auto sim = demo::physics{log, "physics"};

	auto stage = fg::world{pool};
	stage.add(render);
	stage.add(ground);
	stage.add(sim);

	constexpr auto frame_count = ::std::uint64_t{5};
	for (auto index = ::std::uint64_t{0}; index != frame_count; ++index)
	{
		auto current = fg::frame{stage};
		static_cast<void>(current.run());
		stage.advance();
	}

	auto const events = log.snapshot();
	auto per_frame_ok = true;
	auto slots_cycle = true;
	for (auto index = ::std::uint64_t{0}; index != frame_count; ++index)
	{
		per_frame_ok = per_frame_ok
			&& count_in_frame(events, demo::event_kind::begin, index) == 1
			&& count_in_frame(events, demo::event_kind::submit, index) == 1
			&& count_in_frame(events, demo::event_kind::fence, index) == 1
			&& count_in_frame(events, demo::event_kind::record, index) == 1;

		for (auto&& entry : events)
		{
			if (entry._frame == index && entry._kind != demo::event_kind::compute)
			{
				slots_cycle = slots_cycle && entry._slot == index % demo::renderer::slot_count;
			}
		}
	}

	check(per_frame_ok, "每帧恰好一次 begin / record / submit / fence");
	check(slots_cycle, "slot 按 3 格环轮转");
	check(ground._released == frame_count, "每帧都清理了一次");
	check(sim._steps == frame_count, "纯计算 entity 每帧都跑");
	check(!render.any_slot_busy(), "收尾时没有 slot 停在占用态");
}

// --------------------------------------------------------- 8. 动态名单

auto scenario_dynamic_roster() -> void
{
	section("8. 动态名单（帧与帧之间增删 entity）");

	auto pool = ::exec::static_thread_pool{4};
	auto log = demo::render_log{};
	auto render = demo::renderer{log};
	auto ground = demo::terrain{render, log, "terrain"};
	auto plugin = demo::overlay{render, log, "plugin"};

	auto stage = fg::world{pool};
	stage.add(render);
	stage.add(ground);

	auto const run_one = [&]
	{
		auto current = fg::frame{stage};
		static_cast<void>(current.run());
		stage.advance();
	};

	run_one();                       // 第 0 帧：只有 terrain
	stage.add(plugin);               // 插件进场
	run_one();                       // 第 1 帧：terrain + plugin
	check(stage.remove(ground), "terrain 退场");
	run_one();                       // 第 2 帧：只有 plugin

	auto const events = log.snapshot();
	check(count_in_frame(events, demo::event_kind::record, 0) == 1, "第 0 帧 1 条录制");
	check(count_in_frame(events, demo::event_kind::record, 1) == 2, "第 1 帧 2 条录制");
	check(count_in_frame(events, demo::event_kind::record, 2) == 1, "第 2 帧 1 条录制");
	check(plugin._released == 2, "插件只在它在场的两帧里清理");
	check(!render.any_slot_busy(), "收尾时没有 slot 停在占用态");

	// 依赖的 entity 不在场时是响亮的运行期错误——这是动态名单相对静态 tuple
	// 唯一真正的损失：`static_assert` 变成了 `throw`。
	auto orphan = demo::overlay{render, log, "orphan"};
	auto lonely = fg::world{pool};
	lonely.add(orphan);

	auto caught = ::std::string{};
	try
	{
		auto current = fg::frame{lonely};
		static_cast<void>(current.run());
	}
	catch (::std::exception const& error)
	{
		caught = error.what();
	}

	check(caught.find("未参与本帧") != ::std::string::npos, ::std::format("缺席的依赖被当场点名：{}", caught));
}

}

auto main() -> int
{
	::std::println("frame-graph demo —— 动态 entity 名单 + 静态依赖边");

	scenario_registration_order();
	scenario_conditional_recording();
	scenario_empty_roster();
	scenario_record_failure();
	scenario_external_cancel();
	scenario_late_registration();
	scenario_frame_isolation();
	scenario_dynamic_roster();

	::std::println("");
	if (g_failures == 0)
	{
		::std::println("全部通过。");
		return 0;
	}

	::std::println("{} 项失败。", g_failures);
	return 1;
}
