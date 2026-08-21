#pragma once

#include <mutex>
#include <coroutine>
#include <cassert>
#include <vector>

struct barrier
{
	explicit barrier(::std::size_t expected) : _expected(expected) { _waiters.reserve(_expected); }

	barrier(barrier const&) = delete;
	auto operator=(barrier const&) = delete;
	barrier(barrier&&) noexcept = delete;
	auto operator=(barrier&&) noexcept = delete;
	~barrier() noexcept = default;

	[[nodiscard]] auto arrive_and_wait() noexcept
	{
		struct awaitable
		{
			[[nodiscard]] constexpr static auto await_ready() noexcept { return false; }

			::std::coroutine_handle<> await_suspend(::std::coroutine_handle<> waiter) const noexcept
			{
				assert(_barrier);
				auto lock = ::std::scoped_lock{ _barrier->_mutex };
				assert(_barrier->_waiters.size() < _barrier->_expected);
				_barrier->_waiters.push_back(waiter);
				if (_barrier->_waiters.size() == _barrier->_expected)
					return _barrier->_completion;
				return ::std::noop_coroutine();
			}
			constexpr static auto await_resume() noexcept {}

			barrier* _barrier = nullptr;
		};
		return awaitable{ ._barrier = this };
	}

	[[nodiscard]] auto wait() noexcept
	{
		struct awaitable
		{
			[[nodiscard]] constexpr static auto await_ready() noexcept { return false; }

			::std::coroutine_handle<> await_suspend(::std::coroutine_handle<> waiter) const noexcept
			{
				assert(_barrier);
				auto lock = ::std::scoped_lock{ _barrier->_mutex };
				if (_barrier->_waiters.size() == _barrier->_expected)
					return waiter;
				_barrier->_completion = waiter;
				return ::std::noop_coroutine();
			}
			constexpr auto await_resume() const noexcept { return ::std::vector(::std::move(_barrier->_waiters)); }

			barrier* _barrier = nullptr;
		};
		return awaitable{ ._barrier = this };
	}

	::std::size_t _expected{};
	::std::mutex _mutex{};
	::std::vector<::std::coroutine_handle<>> _waiters{};
	::std::coroutine_handle<> _completion = ::std::noop_coroutine();
};