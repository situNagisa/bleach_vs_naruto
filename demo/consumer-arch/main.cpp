#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <print>
#include <thread>
#include <utility>
#include <vector>

#include <exec/static_thread_pool.hpp>
#include <nagisa/concurrency/concurrency.h>
#include <stdexec/execution.hpp>

namespace bvn::demo::consumer_arch
{
using namespace ::std::chrono_literals;
namespace nc = ::nagisa::concurrency;

struct consumer_promise;

template<class Promise, class Parent>
using consumer_awaitable = nc::build_awaitable_t<
	Promise,
	Parent,
	nc::awaitable_traits::ready_if_done,
	nc::awaitable_traits::capture_scheduler,
	nc::awaitable_traits::capture_inplace_stop_token,
	nc::awaitable_traits::this_then_parent,
	nc::awaitable_traits::run_this,
	nc::awaitable_traits::release_value,
	nc::awaitable_traits::rethrow_exception,
	nc::awaitable_traits::destroy_after_resumed>;

using consumer_task = nc::basic_task<consumer_promise, consumer_awaitable>;

/// Local task composition matching bvn::gameplay::task while keeping this demo standalone.
struct consumer_promise
	: nc::promises::lazy
	, nc::promises::exception<false>
	, nc::promises::value<void>
	, nc::promises::jump_to_continuation<>
	, nc::promises::return_object_from_handle<consumer_promise, consumer_task>
	, nc::promises::with_scheduler<::stdexec::inline_scheduler>
	, nc::promises::with_stop_token<::stdexec::inplace_stop_token>
	, nc::promises::with_await_transform<consumer_promise>
{
	using scheduler_base = nc::promises::with_scheduler<::stdexec::inline_scheduler>;
	using stop_token_base = nc::promises::with_stop_token<::stdexec::inplace_stop_token>;

	constexpr explicit(false) consumer_promise() noexcept = default;

	constexpr explicit(false) consumer_promise(auto&&...)
		noexcept(::std::is_nothrow_default_constructible_v<scheduler_base> && ::std::is_nothrow_default_constructible_v<stop_token_base>)
		requires ::std::default_initializable<scheduler_base> && ::std::default_initializable<stop_token_base>
		: scheduler_base()
		, stop_token_base()
	{}

	constexpr explicit(false) consumer_promise(auto&& environment, auto&&...)
		noexcept(::std::is_nothrow_constructible_v<scheduler_base, decltype(environment)> && ::std::is_nothrow_constructible_v<stop_token_base, decltype(environment)>)
		requires ::std::constructible_from<scheduler_base, decltype(environment)> && ::std::constructible_from<stop_token_base, decltype(environment)>
		: scheduler_base(environment)
		, stop_token_base(environment)
	{}

	constexpr auto get_env() const noexcept
	{
		return ::stdexec::env{scheduler_base::get_env(), stop_token_base::get_env()};
	}
};
/// A synchronous coordinator plus copyable non-owning arrival tokens.
struct phase_barrier
{
	struct state
	{
		explicit state(::std::size_t expected) : _expected(expected)
		{
			assert(expected != 0);
			_waiters.reserve(expected);
		}

		::std::size_t _expected;
		::std::atomic_size_t _arrived = 0;
		::std::atomic_bool _resumed = false;
		::std::mutex _mutex;
		::std::vector<::std::coroutine_handle<>> _waiters;
	};

	struct arrival_awaitable
	{
		[[nodiscard]] constexpr auto await_ready() const noexcept -> bool
		{
			return false;
		}

		auto await_suspend(::std::coroutine_handle<> waiter) const noexcept -> bool
		{
			{
				auto lock = ::std::scoped_lock{_state->_mutex};
				assert(!_state->_resumed.load(::std::memory_order_relaxed));
				assert(_state->_waiters.size() < _state->_expected);
				_state->_waiters.push_back(waiter);
			}

			auto previous = _state->_arrived.fetch_add(1, ::std::memory_order_release);
			assert(previous < _state->_expected);
			return true;
		}

		constexpr auto await_resume() const noexcept -> void {}

		state* _state = nullptr;
	};

	struct arrival_token
	{
		[[nodiscard]] auto arrive() const noexcept -> arrival_awaitable
		{
			assert(_state);
			return arrival_awaitable{._state = _state};
		}

		state* _state = nullptr;
	};

	explicit phase_barrier(::std::size_t expected)
		: _state(::std::make_unique<state>(expected))
	{}

	phase_barrier(phase_barrier const&) = delete;
	auto operator=(phase_barrier const&) -> phase_barrier& = delete;
	phase_barrier(phase_barrier&&) noexcept = default;
	auto operator=(phase_barrier&&) noexcept -> phase_barrier& = default;

	[[nodiscard]] auto token() const noexcept -> arrival_token
	{
		assert(_state);
		return arrival_token{._state = _state.get()};
	}

	/// Polls synchronously; it never resumes a consumer.
	auto wait() const noexcept -> void
	{
		assert(_state);
		while (_state->_arrived.load(::std::memory_order_acquire) != _state->_expected)
		{
			::std::this_thread::yield();
		}
	}

	/// Resumes each registered consumer inline on the calling thread.
	auto resume_waiters() -> void
	{
		wait();
		auto was_resumed = _state->_resumed.exchange(true, ::std::memory_order_acq_rel);
		assert(!was_resumed);

		auto waiters = ::std::vector<::std::coroutine_handle<>>{};
		{
			auto lock = ::std::scoped_lock{_state->_mutex};
			waiters.swap(_state->_waiters);
		}
		assert(waiters.size() == _state->_expected);

		for (auto waiter : waiters)
		{
			assert(waiter);
			waiter.resume();
		}
	}

	::std::unique_ptr<state> _state;
};

struct frame_context
{
	frame_context(::std::size_t frame_index, ::std::size_t consumer_count)
		: _frame_index(frame_index)
		, _compute_done(consumer_count)
		, _render_done(consumer_count)
	{}

	::std::size_t _frame_index;
	::std::size_t _frame_slot = 0;
	::std::thread::id _render_thread;
	phase_barrier _compute_done;
	phase_barrier _render_done;
};

struct secondary_command_range
{
	secondary_command_range(
		::std::size_t entity_id,
		bool& active,
		::std::size_t frame_slot,
		::std::size_t first,
		::std::size_t count
	) noexcept
		: _entity_id(entity_id)
		, _active(&active)
		, _frame_slot(frame_slot)
		, _first(first)
		, _count(count)
	{}

	secondary_command_range(secondary_command_range const&) = delete;
	auto operator=(secondary_command_range const&) -> secondary_command_range& = delete;
	secondary_command_range(secondary_command_range&& other) noexcept
		: _entity_id(other._entity_id)
		, _active(::std::exchange(other._active, nullptr))
		, _frame_slot(other._frame_slot)
		, _first(other._first)
		, _count(other._count)
	{}
	auto operator=(secondary_command_range&&) -> secondary_command_range& = delete;

	~secondary_command_range() noexcept
	{
		if (_active == nullptr)
		{
			return;
		}

		assert(*_active);
		*_active = false;
		::std::println(
			"                  entity {} release cmds     slot={} range=[{}, {}) tid={}",
			_entity_id,
			_frame_slot,
			_first,
			_first + _count,
			::std::this_thread::get_id()
		);
	}

	::std::size_t _entity_id;
	bool* _active;
	::std::size_t _frame_slot;
	::std::size_t _first;
	::std::size_t _count;
};

struct compute_snapshot
{
	::std::size_t _frame_index;
	unsigned _value;
};

struct entity
{
	using scheduler = ::exec::static_thread_pool::scheduler;

	explicit entity(::std::size_t id) noexcept : _id(id) {}

	[[nodiscard]] auto compute(::std::size_t frame_index) noexcept -> compute_snapshot
	{
		::std::println(
			"[frame {}] entity {} compute          tid={}",
			frame_index,
			_id,
			::std::this_thread::get_id()
		);
		::std::this_thread::sleep_for(2ms);
		return compute_snapshot{._frame_index = frame_index, ._value = static_cast<unsigned>(frame_index * 10 + _id)};
	}

	[[nodiscard]] auto allocate_secondary_commands(frame_context const& frame) noexcept -> secondary_command_range
	{
		assert(!_range_active);
		_range_active = true;
		auto first = _next_command;
		_next_command += 2;
		::std::println(
			"[frame {}] entity {} allocate cmds    slot={} range=[{}, {}) tid={}",
			frame._frame_index,
			_id,
			frame._frame_slot,
			first,
			first + 2,
			::std::this_thread::get_id()
		);
		return secondary_command_range{_id, _range_active, frame._frame_slot, first, 2};
	}

	auto render(
		compute_snapshot const& snapshot,
		frame_context const& frame,
		secondary_command_range const& commands
	) noexcept -> void
	{
		assert(snapshot._frame_index == frame._frame_index);
		assert(commands._frame_slot == frame._frame_slot);
		assert(::std::this_thread::get_id() != frame._render_thread);
		::std::this_thread::sleep_for(2ms);
		::std::println(
			"[frame {}] entity {} record secondary snapshot={} tid={}",
			frame._frame_index,
			_id,
			snapshot._value,
			::std::this_thread::get_id()
		);
	}

	[[nodiscard]] auto consumer(
		scheduler start_scheduler,
		frame_context& frame
	) noexcept -> consumer_task
	{
		auto snapshot = compute(frame._frame_index);

		co_await frame._compute_done.token().arrive();
		::std::println(
			"[frame {}] entity {} compute resumed  inline tid={}",
			frame._frame_index,
			_id,
			::std::this_thread::get_id()
		);

		co_await (::stdexec::just() | ::stdexec::continues_on(start_scheduler));
		assert(::std::this_thread::get_id() != frame._render_thread);

		{
			auto commands = allocate_secondary_commands(frame);
			render(snapshot, frame, commands);

			co_await frame._render_done.token().arrive();
			assert(::std::this_thread::get_id() == frame._render_thread);
			::std::println(
				"[frame {}] entity {} render resumed   inline tid={}",
				frame._frame_index,
				_id,
				::std::this_thread::get_id()
			);

			co_await (::stdexec::just() | ::stdexec::continues_on(start_scheduler));
			assert(::std::this_thread::get_id() != frame._render_thread);
		}

		co_return;
	}

	::std::size_t _id;
	::std::size_t _next_command = 0;
	bool _range_active = false;
};

auto coordinate_frame(frame_context& frame) -> void
{
	frame._frame_slot = frame._frame_index % 2;
	frame._render_thread = ::std::this_thread::get_id();

	::std::println("[frame {}] begin_frame                     tid={}", frame._frame_index, frame._render_thread);
	::std::println("[frame {}] resume compute waiters inline   tid={}", frame._frame_index, ::std::this_thread::get_id());
	frame._compute_done.resume_waiters();

	frame._render_done.wait();
	::std::println("[frame {}] render_all_done                 tid={}", frame._frame_index, ::std::this_thread::get_id());
	::std::println("[frame {}] end_frame / submit / present    tid={}", frame._frame_index, ::std::this_thread::get_id());

	::std::println("[frame {}] resume render waiters inline    tid={}", frame._frame_index, ::std::this_thread::get_id());
	frame._render_done.resume_waiters();
}

auto run() -> int
{
	constexpr auto entity_count = ::std::size_t{2};
	constexpr auto frame_count = ::std::size_t{3};

	auto compute_pool = ::exec::static_thread_pool{entity_count};
	auto render_pool = ::exec::static_thread_pool{1};
	auto first = entity{1};
	auto second = entity{2};
	auto entities = ::std::array<entity*, entity_count>{&first, &second};

	::std::println("consumer-arch demo: two-phase suspend/resume, {} frames", frame_count);
	for (auto frame_index = ::std::size_t{}; frame_index < frame_count; ++frame_index)
	{
		auto frame = frame_context{frame_index, entity_count};
		auto consumers = ::stdexec::simple_counting_scope{};
		auto compute_scheduler = compute_pool.get_scheduler();

		for (auto index = ::std::size_t{}; index < entity_count; ++index)
		{
				auto consumer = ::stdexec::starts_on(
				compute_scheduler,
				entities[index]->consumer(
					compute_scheduler,
					frame
				)
			) | ::stdexec::upon_error(
				[](auto&&) noexcept
				{
					::std::terminate();
				}
			);
			::stdexec::spawn(
				::std::move(consumer),
				consumers.get_token()
			);
		}
		consumers.close();

		frame._compute_done.wait();
		::std::println("[frame {}] produce_all_done observed     tid={}", frame_index, ::std::this_thread::get_id());

		auto render_work = ::stdexec::just()
			| ::stdexec::then(
				[&frame]
				{
					coordinate_frame(frame);
				}
			);
		auto result = ::stdexec::sync_wait(
			::stdexec::starts_on(render_pool.get_scheduler(), ::std::move(render_work))
		);
		assert(result.has_value());

		auto consumers_result = ::stdexec::sync_wait(consumers.join());
		assert(consumers_result.has_value());
		assert(!first._range_active);
		assert(!second._range_active);
		::std::println("[frame {}] all consumers retired\n", frame_index);
	}

	::std::println("consumer-arch demo completed");
	return 0;
}
}

auto main() -> int
{
	return ::bvn::demo::consumer_arch::run();
}
