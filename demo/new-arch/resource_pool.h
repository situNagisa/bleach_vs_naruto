#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>


/// 固定容量的资源池：`acquire()` 拿一份，租约析构时还回去。
///
/// ```cpp
/// auto pool = resource_pool<frame_slot>{3, [&](auto) { return make_slot(device); }};
///
/// auto sender = pool.acquire()
///     | ::stdexec::then([](auto lease) { use(*lease); });   // 租约出作用域 = 归还
/// ```
///
/// **职责只有三件**：装着 N 份东西、按先来先到把空闲的那份交出去、租约析构时收回来。
/// 资源怎么造、拿到之后要不要再初始化一遍（比如 `vkAcquireNextImageKHR`）、归还前要不要
/// 等 GPU —— 都是使用者的事，用 `then` / `let_value` 接在 `acquire()` 后面即可。
///
/// ## 唤醒等待者这件事：池子自己做
///
/// 协程版必须有个 `run_once()`，是因为等待者只留下一个裸的 `coroutine_handle<>`，
/// 那东西不带任何信息，只能由外部挑个时机 `.resume()`。
///
/// sender/receiver 里**没有"恢复"这个动作**，只有"完成一个操作"：等待者就是一个 op-state，
/// 唤醒它 = 对它的 receiver 调 `set_value(租约)`。池子手里已经有全部信息，把等待者交出去
/// 只能让调用方去做同一件事，还得先把 op-state 的类型擦掉——纯亏。
/// `::std::counting_semaphore::release`、`::exec::async_scope`、
/// `async_manual_reset_event::set` 都是自己完成等待者的。
///
/// 真正值得关心的问题是"**续体在哪个线程跑**"。这里的答案是：在调用归还的那个线程上
/// 同步跑完。想换线程，使用者自己写 `pool.acquire() | ::stdexec::continues_on(sched)`——
/// 这比池子内置一套调度策略更明确、也更可组合。
///
/// ## 取消
///
/// 排队期间外层请求取消，等待者会把自己从队列里摘掉并 `set_stopped`。已经拿到租约之后
/// 取消不再有效——那时操作已经完成了。
template <class ValueType>
struct resource_pool
{
	struct lease;

	/// 等待队列的节点。**op-state 自己就是节点**，排队不分配。
	struct waiter
	{
		waiter* _next = nullptr;
		void (*_complete)(waiter*, ValueType*) noexcept = nullptr;
	};

	// ---------------------------------------------------------------- 租约

	/// 拿到的那份资源。只可移动，析构即归还。
	struct lease
	{
		resource_pool* _pool = nullptr;
		ValueType* _value = nullptr;

		lease() = default;

		lease(resource_pool& pool, ValueType& value) noexcept
			: _pool(&pool)
			, _value(&value)
		{
		}

		lease(lease&& other) noexcept
			: _pool(::std::exchange(other._pool, nullptr))
			, _value(::std::exchange(other._value, nullptr))
		{
		}

		auto operator=(lease&& other) noexcept -> lease&
		{
			auto discarded = ::std::move(*this);
			_pool = ::std::exchange(other._pool, nullptr);
			_value = ::std::exchange(other._value, nullptr);
			return *this;
		}

		~lease()
		{
			if (_pool != nullptr)
			{
				_pool->_release(*_value);
			}
		}

		[[nodiscard]] explicit operator bool() const noexcept { return _pool != nullptr; }
		[[nodiscard]] auto operator*() const noexcept -> ValueType& { return *_value; }
		[[nodiscard]] auto operator->() const noexcept -> ValueType* { return _value; }
		[[nodiscard]] auto get() const noexcept -> ValueType* { return _value; }
	};

	// ---------------------------------------------------------------- sender

	struct acquire_sender
	{
		using sender_concept = ::stdexec::sender_t;
		using completion_signatures = ::stdexec::completion_signatures<
			::stdexec::set_value_t(lease),
			::stdexec::set_stopped_t()>;

		resource_pool* _pool;

		template <class ReceiverType>
		struct operation : waiter
		{
			using operation_state_concept = ::stdexec::operation_state_t;

			/// 排队期间外层取消：把自己摘掉，然后 `set_stopped`。
			struct on_stop
			{
				operation* _operation;

				auto operator()() const noexcept -> void
				{
					if (!_operation->_pool->_unlink(_operation))
					{
						// 没在队列里 = 归还那条路已经把我们摘走了，完成由它负责。
						return;
					}
					// 从回调内部销毁这个回调本身是允许的（`__removed_during_callback_`），
					// upstream 的 `when_all` 也是这么干的。
					_operation->_on_stop.reset();
					::stdexec::set_stopped(::std::move(_operation->_receiver));
				}
			};

			using token_type = ::stdexec::stop_token_of_t<::stdexec::env_of_t<ReceiverType>>;
			using callback_type = typename token_type::template callback_type<on_stop>;

			/// 令牌压根不会停的话，整套回调都是死重量。
			static constexpr bool _uses_stop_callback = !::stdexec::unstoppable_token<token_type>;

			struct no_callback
			{
			};

			using slot_type =
				::std::conditional_t<_uses_stop_callback, ::std::optional<callback_type>, no_callback>;

			resource_pool* _pool;
			ReceiverType _receiver;
			[[no_unique_address]] slot_type _on_stop{};

			operation(resource_pool& pool, ReceiverType receiver)
				: _pool(&pool)
				, _receiver(::std::move(receiver))
			{
				this->_complete = [](waiter* self, ValueType* value) noexcept
				{
					auto* const target = static_cast<operation*>(self);
					if constexpr (_uses_stop_callback)
					{
						// 先注销：`~inplace_stop_callback` 会等正在跑的取消回调返回，
						// 那个回调发现自己已不在队列里就直接退出，所以不会互等。
						target->_on_stop.reset();
					}
					::stdexec::set_value(::std::move(target->_receiver), lease{*target->_pool, *value});
				};
			}

			operation(operation&&) = delete;
			auto operator=(operation&&) -> operation& = delete;

			auto start() & noexcept -> void
			{
				if (auto* const value = _pool->_take_or_enqueue(*this))
				{
					::stdexec::set_value(::std::move(_receiver), lease{*_pool, *value});
					return;
				}

				// 排上队了。装取消回调——装的过程里回调就可能立刻跑（令牌已停止），
				// 那条路会把自己摘掉并完成，所以之后不能再碰 *this。
				if constexpr (_uses_stop_callback)
				{
					_on_stop.emplace(
						::stdexec::get_stop_token(::stdexec::get_env(_receiver)), on_stop{this});
				}
			}
		};

		template <::stdexec::receiver ReceiverType>
		[[nodiscard]] auto connect(ReceiverType receiver) const -> operation<ReceiverType>
		{
			return operation<ReceiverType>{*_pool, ::std::move(receiver)};
		}
	};

	// ------------------------------------------------------------------ 池子

	::std::vector<ValueType> _values;
	::std::mutex _mutex;
	::std::vector<ValueType*> _free;
	waiter* _head = nullptr;
	waiter* _tail = nullptr;

	/// 造 `count` 份资源，第 i 份来自 `factory(i)`。
	template <class Factory>
		requires ::std::is_invocable_v<Factory&, ::std::size_t>
	resource_pool(::std::size_t count, Factory factory)
	{
		// 只 reserve 一次、之后再不改动，于是元素地址在整个池子生命周期内稳定——
		// 租约和等待者攥的都是 `ValueType*`。
		_values.reserve(count);
		_free.reserve(count);
		for (auto index = ::std::size_t{0}; index != count; ++index)
		{
			_values.push_back(factory(index));
		}
		for (auto&& value : _values)
		{
			_free.push_back(&value);
		}
	}

	resource_pool(resource_pool&&) = delete;
	auto operator=(resource_pool&&) -> resource_pool& = delete;

	~resource_pool()
	{
		assert(_head == nullptr && "resource pool: 还有等待者没完成");
		assert(_free.size() == _values.size() && "resource pool: 还有租约没归还");
	}

	/// 排队等一份资源。可多次连接，每次连接是一次独立的申请。
	[[nodiscard]] auto acquire() noexcept -> acquire_sender { return acquire_sender{this}; }

	/// 有空闲就立刻拿一份，没有就返回 `nullopt`——不排队。
	[[nodiscard]] auto try_acquire() -> ::std::optional<lease>
	{
		auto const guard = ::std::lock_guard{_mutex};
		if (_free.empty())
		{
			return ::std::nullopt;
		}
		auto* const value = _free.back();
		_free.pop_back();
		return ::std::optional<lease>{::std::in_place, *this, *value};
	}

	[[nodiscard]] auto size() const noexcept -> ::std::size_t { return _values.size(); }

	[[nodiscard]] auto available() -> ::std::size_t
	{
		auto const guard = ::std::lock_guard{_mutex};
		return _free.size();
	}

	/// 有空闲就直接给，没有就把 `target` 挂到队尾。返回拿到的资源或 `nullptr`。
	[[nodiscard]] auto _take_or_enqueue(waiter& target) -> ValueType*
	{
		auto const guard = ::std::lock_guard{_mutex};

		if (!_free.empty())
		{
			auto* const value = _free.back();
			_free.pop_back();
			return value;
		}

		target._next = nullptr;
		if (_tail == nullptr)
		{
			_head = &target;
		}
		else
		{
			_tail->_next = &target;
		}
		_tail = &target;
		return nullptr;
	}

	/// 把 `target` 从队列里摘掉。返回它当时是否还在队列里。
	[[nodiscard]] auto _unlink(waiter* target) -> bool
	{
		auto const guard = ::std::lock_guard{_mutex};

		auto** link = &_head;
		auto* previous = static_cast<waiter*>(nullptr);
		while (*link != nullptr)
		{
			if (*link == target)
			{
				*link = target->_next;
				if (_tail == target)
				{
					_tail = previous;
				}
				return true;
			}
			previous = *link;
			link = &(*link)->_next;
		}
		return false;
	}

	/// 归还。有人在等就**直接转交**（资源不回空闲表），否则放回空闲表。
	auto _release(ValueType& value) noexcept -> void
	{
		auto* next = static_cast<waiter*>(nullptr);
		{
			auto const guard = ::std::lock_guard{_mutex};
			if (_head != nullptr)
			{
				next = _head;
				_head = next->_next;
				if (_head == nullptr)
				{
					_tail = nullptr;
				}
			}
			else
			{
				_free.push_back(&value);
			}
		}

		// 出锁再完成：等待者的续体在**当前线程**同步跑完，期间可能又去 acquire / release。
		if (next != nullptr)
		{
			next->_complete(next, &value);
		}
	}
};
