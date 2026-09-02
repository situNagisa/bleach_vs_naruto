#pragma once

/// demo 用的三个 entity，代表三类开发者。
///
///  * `physics` —— 纯计算。整个类型里**没有任何 render 概念**：不 include renderer.h，
///    没有 node_ref，没有类型擦除。它只是"一个能生成本帧 job 的东西"。
///  * `terrain` —— 参与渲染。计算 → 录制 → 等 fence 后清资源，三段齐全，
///    并且本帧可以因为被剔除而**根本不注册录制**。
///  * `overlay` —— 同样参与渲染，但带一组开关（失败 / 拖时间 / 迟到注册），
///    专门用来在 main 里制造异常路径。
///
/// 三个 entity 都不知道彼此，也不知道 main 收集了谁。terrain / overlay 只知道
/// 一件事：手里这个 `renderer&`。本帧的 renderer job 从它的 `_job_slot` 上取。

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <stdexec/execution.hpp>

#include "./frame_graph.h"
#include "./renderer.h"

namespace bvn::frame_graph::demo
{

// ------------------------------------------------------------------ 纯计算

struct physics
{
	struct job
	{
		physics& _physics;
		frame_context _context;

		job(physics& owner, frame_context const& context) noexcept
			: _physics(owner)
			, _context(context)
		{}

		job(job const&) = delete;
		auto operator=(job const&) -> job& = delete;

		[[nodiscard]] auto frame_node() -> node_sender
		{
			return make_node(::stdexec::starts_on(_context._scheduler, ::stdexec::just())
				| ::stdexec::then([this] { _physics.step(_context._index, _context._delta_seconds); }));
		}
	};

	render_log& _log;
	::std::string _name;
	::std::uint64_t _steps = 0;

	physics(render_log& log, ::std::string name) noexcept
		: _log(log)
		, _name(::std::move(name))
	{}

	physics(physics const&) = delete;
	auto operator=(physics const&) -> physics& = delete;

	[[nodiscard]] auto begin_frame(frame_context const& context) noexcept -> job
	{
		return job{*this, context};
	}

	auto step(::std::uint64_t frame, float delta_seconds) -> void
	{
		static_cast<void>(delta_seconds);
		++_steps;
		_log.push(event_kind::compute, frame, 0, _name);
	}
};

// -------------------------------------------------------------- 参与渲染

struct terrain
{
	struct job
	{
		terrain& _terrain;
		frame_context _context;
		node_ref _compute;
		int _mesh = 0;
		int _staging = 0;

		job(terrain& owner, frame_context const& context) noexcept
			: _terrain(owner)
			, _context(context)
		{}

		job(job const&) = delete;
		auto operator=(job const&) -> job& = delete;

		[[nodiscard]] auto frame_node() -> node_sender
		{
			// 阶段 B：所有 job 都已就位，这里才去取"本帧的 renderer job"。
			// 这句是本 demo 的支点——依赖是具体类型直连，而 entity 名单是动态的。
			auto&& render = _terrain._renderer._job_slot.get();

			// 本帧被剔除就根本不注册。静态的"收集所有录制者"表达不了这件事。
			if (_terrain._visible)
			{
				render.recorders().add(record_node(render));
			}

			return make_node(render.fence_node()
				| ::stdexec::then([this] { _terrain.release(_context._index, _staging); }));
		}

		/// 只有一个消费者（录制名单），所以不需要 `split`，也就不需要 `node_ref`。
		[[nodiscard]] auto record_node(renderer::job& render) -> node_sender
		{
			return make_node(::stdexec::when_all(compute_node(), render.begin_node())
				// 这个 `continues_on` 不是可选的。名单是启动期才读的，读的时候 begin 早就
				// 完成了，于是订阅一个已完成的 `split` 会**原地同步**派发——所有录制者会
				// 串在同一根线程上。换一次调度才能真正并行。
				| ::stdexec::continues_on(_context._scheduler)
				| ::stdexec::then([this, &render]
					{
						_staging = _terrain.record(render.command(), _context._index, _mesh);
					}));
		}

		[[nodiscard]] auto compute_node() -> node_sender
		{
			return _compute.get([&]
			{
				return ::stdexec::starts_on(_context._scheduler, ::stdexec::just())
					| ::stdexec::then([this] { _mesh = _terrain.compute(_context._index, _context._delta_seconds); });
			});
		}
	};

	renderer& _renderer;
	render_log& _log;
	::std::string _name;
	bool _visible = true;
	::std::uint64_t _released = 0;

	terrain(renderer& target, render_log& log, ::std::string name) noexcept
		: _renderer(target)
		, _log(log)
		, _name(::std::move(name))
	{}

	terrain(terrain const&) = delete;
	auto operator=(terrain const&) -> terrain& = delete;

	[[nodiscard]] auto begin_frame(frame_context const& context) noexcept -> job
	{
		return job{*this, context};
	}

	[[nodiscard]] auto compute(::std::uint64_t frame, float delta_seconds) -> int
	{
		static_cast<void>(delta_seconds);
		_log.push(event_kind::compute, frame, 0, _name);
		return static_cast<int>(frame);
	}

	[[nodiscard]] auto record(command_buffer const& command, ::std::uint64_t frame, int mesh) -> int
	{
		_renderer.record(command, frame, _name);
		return mesh + 1;
	}

	auto release(::std::uint64_t frame, int staging) -> void
	{
		static_cast<void>(frame);
		static_cast<void>(staging);
		++_released;
	}
};

// ------------------------------------------------- 参与渲染（带故障开关）

struct overlay
{
	struct job
	{
		overlay& _overlay;
		frame_context _context;
		int _staging = 0;

		job(overlay& owner, frame_context const& context) noexcept
			: _overlay(owner)
			, _context(context)
		{}

		job(job const&) = delete;
		auto operator=(job const&) -> job& = delete;

		[[nodiscard]] auto frame_node() -> node_sender
		{
			auto&& render = _overlay._renderer._job_slot.get();

			if (_overlay._visible)
			{
				render.recorders().add(record_node(render));
			}

			return make_node(render.fence_node()
				| ::stdexec::then([this] { ++_overlay._released; }));
		}

		[[nodiscard]] auto record_node(renderer::job& render) -> node_sender
		{
			return make_node(render.begin_node()
				| ::stdexec::continues_on(_context._scheduler)
				| ::stdexec::then([this, &render]
					{
						if (_overlay._record_delay.count() != 0)
						{
							::std::this_thread::sleep_for(_overlay._record_delay);
						}

						if (_overlay._fail)
						{
							throw ::std::runtime_error{_overlay._name + ": 录制失败"};
						}

						if (_overlay._late_register)
						{
							// 名单已经在 render.end 启动时封存了，这里必然抛。
							render.recorders().add(render.begin_node());
						}

						_overlay._renderer.record(render.command(), _context._index, _overlay._name);
						_staging = 1;
					}));
		}
	};

	renderer& _renderer;
	render_log& _log;
	::std::string _name;
	bool _visible = true;
	bool _fail = false;
	bool _late_register = false;
	::std::chrono::milliseconds _record_delay{0};
	::std::uint64_t _released = 0;

	overlay(renderer& target, render_log& log, ::std::string name) noexcept
		: _renderer(target)
		, _log(log)
		, _name(::std::move(name))
	{}

	overlay(overlay const&) = delete;
	auto operator=(overlay const&) -> overlay& = delete;

	[[nodiscard]] auto begin_frame(frame_context const& context) noexcept -> job
	{
		return job{*this, context};
	}
};

static_assert(frame_entity<physics> && frame_job<job_of<physics>>);
static_assert(frame_entity<terrain> && frame_job<job_of<terrain>>);
static_assert(frame_entity<overlay> && frame_job<job_of<overlay>>);

// 纯计算 entity 不暴露 job 槽位——没人依赖它，就不为它引入任何多余概念。
static_assert(!exposes_current_job<physics>);

}
