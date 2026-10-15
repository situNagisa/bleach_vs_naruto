#pragma once

#include <utility>

#include <stdexec/execution.hpp>

/// 一帧的通用组合：input.poll → scene.update →（与 renderer.begin 并发）→ scene.record → renderer.end。
///
/// 框架只规定这条骨架，不认识任何具体类型：
/// - `Input`    提供 `poll()`，sender<输入值...>
/// - `Scene`    提供 `update(输入值...)` -> sender<state, delta...>，
///              `record(frame_slot&, state const&)` -> sender<recording>
/// - `Renderer` 提供 `frame(upstream, record)`
///
/// 一个场景想不画（fighting）、想画多层、想并发多个录制者，就不用这个函数，自己组合。
template <class Input, class Scene, class Renderer>
[[nodiscard]] auto scene_frame(Input& controls, Scene& scene, Renderer& draw)
{
	auto updated = controls.poll()
		| ::stdexec::let_value([&scene](auto const&... values) { return scene.update(values...); })
		| ::stdexec::then([](auto const& snapshot, auto const&...) { return snapshot; });
	return draw.frame(::std::move(updated), [&scene](auto& slot, auto const& snapshot)
		{
			return scene.record(slot, snapshot);
		});
}
