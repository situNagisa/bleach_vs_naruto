#pragma once

/// demo 用的假 renderer entity。
///
/// 它替 vulkan 站位：开帧拿 command buffer、收录制、提交、**在帧末**等 fence。
/// 不碰真设备，只往 `render_log` 里记事件，好让 main 断言时序；
/// 另外用 `_slot_busy` 模拟 GPU 占用——`open_command_buffer` 断言这个 slot 上一轮的
/// fence 已经等过了，于是"某条完成路径漏等 fence"会当场炸，而不是变成偶发的资源复用 bug。

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "./frame_graph.h"

namespace bvn::frame_graph::demo
{

enum class event_kind
{
	begin,
	record,
	submit,
	fence,
	compute,
};

struct event
{
	event_kind _kind = event_kind::begin;
	::std::uint64_t _frame = 0;
	::std::size_t _slot = 0;
	::std::string _who;
};

/// 事件流。所有断言都建立在它上面。
struct render_log
{
	mutable ::std::mutex _mutex;
	::std::vector<event> _events;

	auto push(event_kind kind, ::std::uint64_t frame, ::std::size_t slot, ::std::string_view who) -> void
	{
		auto const lock = ::std::lock_guard{_mutex};
		_events.push_back(event{._kind = kind, ._frame = frame, ._slot = slot, ._who = ::std::string{who}});
	}

	auto clear() -> void
	{
		auto const lock = ::std::lock_guard{_mutex};
		_events.clear();
	}

	[[nodiscard]] auto snapshot() const -> ::std::vector<event>
	{
		auto const lock = ::std::lock_guard{_mutex};
		return _events;
	}

	[[nodiscard]] auto count(event_kind kind) const -> ::std::size_t
	{
		auto const lock = ::std::lock_guard{_mutex};
		auto total = ::std::size_t{0};
		for (auto&& entry : _events)
		{
			if (entry._kind == kind)
			{
				++total;
			}
		}

		return total;
	}

	/// 某类事件里是否出现过某个名字。
	[[nodiscard]] auto has(event_kind kind, ::std::string_view who) const -> bool
	{
		auto const lock = ::std::lock_guard{_mutex};
		for (auto&& entry : _events)
		{
			if (entry._kind == kind && entry._who == who)
			{
				return true;
			}
		}

		return false;
	}

	/// 第一个匹配事件的下标，找不到返回 `_events.size()`。用来断言先后。
	[[nodiscard]] auto index_of(event_kind kind, ::std::string_view who = {}) const -> ::std::size_t
	{
		auto const lock = ::std::lock_guard{_mutex};
		for (auto index = ::std::size_t{0}; index != _events.size(); ++index)
		{
			if (_events[index]._kind == kind && (who.empty() || _events[index]._who == who))
			{
				return index;
			}
		}

		return _events.size();
	}
};

/// 假的 command buffer 句柄。
struct command_buffer
{
	::std::size_t _slot = 0;
	bool _open = false;
};

struct renderer
{
	static constexpr auto slot_count = ::std::size_t{3};

	struct job
	{
		/// 一帧走过的三段。用来把"注册 / 订阅迟到"变成响亮的异常而不是静默丢失。
		enum class phase
		{
			constructing,
			recording,
			submitting,
		};

		renderer& _renderer;
		frame_context _context;
		::std::size_t _slot = 0;
		command_buffer _command;
		node_roster _recorders;
		::std::atomic<phase> _phase{phase::constructing};
		node_ref _begin;
		node_ref _end;
		node_ref _fence;

		job(renderer& owner, frame_context const& context) noexcept
			: _renderer(owner)
			, _context(context)
			, _slot(static_cast<::std::size_t>(context._index % slot_count))
		{}

		job(job const&) = delete;
		auto operator=(job const&) -> job& = delete;

		/// render begin。录制者 `when_all` 它来表达"录制发生在开帧之后"。
		[[nodiscard]] auto begin_node() -> node_sender
		{
			return _begin.get([&]
			{
				// 取消在这里咬住：begin 没跑 ⇒ 没有录制 ⇒ 没有提交，
				// 而 end / fence 仍然沿 stopped 路走完，fence 照样等。
				return cancellable(::stdexec::starts_on(_context._scheduler, ::stdexec::just())
					| ::stdexec::then([this] { _command = _renderer.open_command_buffer(_slot, _context._index); }),
					_context);
			});
		}

		/// 本帧的录制名单。构建期往里挂，启动期才被读走，所以谁先挂谁后挂无所谓。
		[[nodiscard]] auto recorders() -> node_roster&
		{
			if (_phase.load(::std::memory_order_acquire) != phase::constructing)
			{
				throw ::std::logic_error{"renderer: 录制名单已封存，注册来晚了"};
			}

			return _recorders;
		}

		/// 只在录制节点里读。写它的是 begin 节点，而录制节点排在 begin 之后，所以无竞争。
		[[nodiscard]] auto command() const noexcept -> command_buffer const&
		{
			return _command;
		}

		/// render end：等齐本帧所有录制者，然后提交。
		[[nodiscard]] auto end_node() -> node_sender
		{
			return _end.get([&]
			{
				return begin_node()
					| ::stdexec::let_value([this]
						{
							// 分界线在这儿：`let_value` 的体是**启动后**才跑的，
							// 于是"读名单"从构图期推迟到了启动期，构建顺序彻底无关。
							_phase.store(phase::recording, ::std::memory_order_release);
							return when_all_range(_recorders.seal());
						})
					| ::stdexec::continues_on(_context._scheduler)
					| ::stdexec::then([this]
						{
							_phase.store(phase::submitting, ::std::memory_order_release);
							_renderer.submit(_slot, _context._index);
						});
			});
		}

		/// 帧末等 fence。三条完成路径都要落到 `wait_fence` 上：
		/// 录制抛异常会取消兄弟节点，取消也会让 end 以 `set_stopped` 完成——
		/// 任何一条漏掉，这个 slot 的 GPU 资源就没人回收，下一轮复用时踩在 GPU 还在读的内存上。
		///
		/// 注意 `continues_on` 只搬运 value 这条路；error / stopped 会绕过调度切换，
		/// 直接在出错的那根线程上跑 `let_error` / `let_stopped`。对 fence 等待来说可以接受。
		[[nodiscard]] auto fence_node() -> node_sender
		{
			return _fence.get([&]
			{
				return end_node()
					| ::stdexec::continues_on(_renderer._fence_pool.get_scheduler())
					| ::stdexec::let_error([this](::std::exception_ptr error)
						{
							_renderer.wait_fence(_slot, _context._index);
							return ::stdexec::just_error(::std::move(error));
						})
					| ::stdexec::let_stopped([this]
						{
							_renderer.wait_fence(_slot, _context._index);
							return ::stdexec::just_stopped();
						})
					| ::stdexec::then([this] { _renderer.wait_fence(_slot, _context._index); });
			});
		}

		[[nodiscard]] auto frame_node() -> node_sender
		{
			return fence_node();
		}
	};

	render_log& _log;
	::exec::static_thread_pool _fence_pool{1};
	::std::array<::std::atomic<bool>, slot_count> _slot_busy{};
	::std::array<::std::atomic<int>, slot_count> _slot_records{};
	::std::atomic<::std::size_t> _submit_count{0};
	::std::atomic<::std::size_t> _fence_count{0};
	job_slot<job> _job_slot;

	explicit renderer(render_log& log) noexcept
		: _log(log)
	{}

	renderer(renderer const&) = delete;
	auto operator=(renderer const&) -> renderer& = delete;

	[[nodiscard]] auto begin_frame(frame_context const& context) noexcept -> job
	{
		return job{*this, context};
	}

	// ---------------------------------------------------------- 假设备接口

	[[nodiscard]] auto open_command_buffer(::std::size_t slot, ::std::uint64_t frame) -> command_buffer
	{
		// 上一轮用这个 slot 的那帧必须已经等过 fence。漏等就会在这里炸。
		assert(!_slot_busy[slot].load(::std::memory_order_acquire));
		_slot_records[slot].store(0, ::std::memory_order_relaxed);
		_log.push(event_kind::begin, frame, slot, "renderer");
		return command_buffer{._slot = slot, ._open = true};
	}

	auto record(command_buffer const& command, ::std::uint64_t frame, ::std::string_view who) -> void
	{
		assert(command._open);
		_slot_records[command._slot].fetch_add(1, ::std::memory_order_relaxed);
		_log.push(event_kind::record, frame, command._slot, who);
	}

	auto submit(::std::size_t slot, ::std::uint64_t frame) -> void
	{
		_slot_busy[slot].store(true, ::std::memory_order_release);
		_submit_count.fetch_add(1, ::std::memory_order_relaxed);
		_log.push(event_kind::submit, frame, slot, "renderer");
	}

	/// 幂等：整帧被取消时 begin 可能压根没跑过，此时这里只是空转。
	auto wait_fence(::std::size_t slot, ::std::uint64_t frame) -> void
	{
		_slot_busy[slot].store(false, ::std::memory_order_release);
		_fence_count.fetch_add(1, ::std::memory_order_relaxed);
		_log.push(event_kind::fence, frame, slot, "renderer");
	}

	[[nodiscard]] auto records_in_slot(::std::size_t slot) const noexcept -> int
	{
		return _slot_records[slot].load(::std::memory_order_relaxed);
	}

	[[nodiscard]] auto any_slot_busy() const noexcept -> bool
	{
		for (auto&& busy : _slot_busy)
		{
			if (busy.load(::std::memory_order_acquire))
			{
				return true;
			}
		}

		return false;
	}
};

static_assert(frame_entity<renderer>);
static_assert(frame_job<job_of<renderer>>);
static_assert(exposes_current_job<renderer>);

}
