#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include "node_sender.h"

namespace bvn::task_graph
{

namespace details
{
/// 存放不可移动对象的手工生命周期槽。
///
/// op-state 是地址敏感的（子接收者持有指向它的指针），既不可拷贝也不可移动，于是
/// `::std::optional::emplace` 这条路走不通——它是直接初始化，会去找移动构造。
/// 这里走 `::new (地址) T(工厂())`：工厂返回 T 的纯右值，C++17 保证省略，
/// 对象直接落在槽里。
template <class Value>
struct manual_lifetime
{
	alignas(Value) ::std::byte _storage[sizeof(Value)];
	bool _engaged = false;

	manual_lifetime() = default;
	manual_lifetime(manual_lifetime const&) = delete;
	auto operator=(manual_lifetime const&) -> manual_lifetime& = delete;

	~manual_lifetime()
	{
		reset();
	}

	/// `factory` 必须返回 `Value` 的纯右值——靠保证省略直接在槽里构造。
	template <class Factory>
	auto construct(Factory&& factory) -> Value&
	{
		assert(!_engaged);
		auto const object = ::new (static_cast<void*>(_storage)) Value(::std::forward<Factory>(factory)());
		_engaged = true;
		return *object;
	}

	auto reset() noexcept -> void
	{
		if (_engaged)
		{
			_engaged = false;
			::std::launder(reinterpret_cast<Value*>(_storage))->~Value();
		}
	}

	[[nodiscard]] auto get() noexcept -> Value&
	{
		assert(_engaged);
		return *::std::launder(reinterpret_cast<Value*>(_storage));
	}
};
}

// ---------------------------------------------------------------- 动态扇入

/// 元素个数到启动期才知道的 `when_all`。
///
/// 为什么不是"逐个 spawn 进 async_scope"：spawn 丢错误、丢取消，而 `scope.on_empty()`
/// 是"空了"不是"汇合"。这里是一个真正的汇合子：一个倒计数器 + 首个非正常完成胜出。
///
/// **跟 `::stdexec::when_all` 的唯一语义差别：孩子出错 / 被取消时不广播取消给兄弟。**
/// 标准 `when_all` 会广播，但那跟"取消不走结构边"直接冲突：兄弟里但凡有个收尾节点
/// （`split` 出来的清理边），被广播停掉就在订阅侧短路，收尾体一次都不跑。而且谁先
/// 完成是竞态，于是漏不漏收尾也成了竞态。这里选 **correctness over fail-fast**：
/// 所有孩子都跑完，第一个非正常完成的结果胜出。想 fail-fast 的话在自己的节点里做。
///
/// 外层要求取消时仍然照转（`_on_stop`）——那是 sender 该有的行为，跟这条无关。
///
/// 孩子同构且不传值，所以完成签名写死，不需要 `transform_completion_signatures`，
/// 也不需要折叠值 variant。
template <class ChildSender>
struct dynamic_when_all_sender
{
	static_assert(::stdexec::sender_in<ChildSender, node_env>);

	using sender_concept = ::stdexec::sender_t;
	using completion_signatures = node_completions;

	enum class completion_kind
	{
		value,
		error,
		stopped,
	};

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		struct child_receiver
		{
			using receiver_concept = ::stdexec::receiver_t;

			operation* _operation;

			auto set_value() noexcept -> void
			{
				_operation->arrive();
			}

			auto set_error(::std::exception_ptr error) noexcept -> void
			{
				_operation->fail(::std::move(error));
				_operation->arrive();
			}

			auto set_stopped() noexcept -> void
			{
				_operation->stopped();
				_operation->arrive();
			}

			[[nodiscard]] auto get_env() const noexcept -> node_env
			{
				return node_env{._stop_token = _operation->_stop_source.get_token()};
			}
		};

		struct forward_stop
		{
			operation* _operation;

			auto operator()() const noexcept -> void
			{
				_operation->_stop_source.request_stop();
			}
		};

		using child_operation_type = ::stdexec::connect_result_t<ChildSender, child_receiver>;
		using outer_token_type = ::stdexec::stop_token_of_t<::stdexec::env_of_t<Receiver>>;
		using stop_callback_type = typename outer_token_type::template callback_type<forward_stop>;

		Receiver _receiver;
		::std::vector<ChildSender> _sources;
		::std::size_t _count = 0;
		::std::unique_ptr<details::manual_lifetime<child_operation_type>[]> _operations;
		::std::atomic<::std::size_t> _pending{0};
		::std::atomic<completion_kind> _kind{completion_kind::value};
		::std::exception_ptr _error;
		::stdexec::inplace_stop_source _stop_source;
		::std::optional<stop_callback_type> _on_stop;

		operation(::std::vector<ChildSender> sources, Receiver receiver)
			: _receiver(::std::move(receiver))
			, _sources(::std::move(sources))
			, _count(_sources.size())
			, _operations(::std::make_unique<details::manual_lifetime<child_operation_type>[]>(_count))
		{}

		operation(operation const&) = delete;
		auto operator=(operation const&) -> operation& = delete;

		auto start() & noexcept -> void
		{
			if (_count == 0)
			{
				::stdexec::set_value(::std::move(_receiver));
				return;
			}

			// +1 是留给下面这个启动循环自己的：不然某个同步完成的孩子会在循环还没走完时
			// 把计数减到 0，触发完成、进而销毁 *this，后面的迭代就踩在死对象上。
			_pending.store(_count + 1, ::std::memory_order_relaxed);
			_on_stop.emplace(::stdexec::get_stop_token(::stdexec::get_env(_receiver)), forward_stop{this});

			try
			{
				// 先全部 connect，再全部 start。反过来的话，第一个孩子同步完成时
				// 后面的孩子还没连上，它们的 op-state 就永远不会存在。
				for (auto index = ::std::size_t{0}; index != _count; ++index)
				{
					_operations[index].construct([&]
					{
						return ::stdexec::connect(::std::move(_sources[index]), child_receiver{this});
					});
				}
			}
			catch (...)
			{
				// 还没 start 的 op-state 直接销毁即可——没启动就没有完成的义务。
				for (auto index = ::std::size_t{0}; index != _count; ++index)
				{
					_operations[index].reset();
				}

				_on_stop.reset();
				::stdexec::set_error(::std::move(_receiver), ::std::current_exception());
				return;
			}

			_sources.clear();

			for (auto index = ::std::size_t{0}; index != _count; ++index)
			{
				::stdexec::start(_operations[index].get());
			}

			arrive();
		}

		/// 首个非正常完成胜出。**不广播取消**——见类型注释。
		auto fail(::std::exception_ptr error) noexcept -> void
		{
			auto expected = completion_kind::value;
			if (_kind.compare_exchange_strong(expected, completion_kind::error, ::std::memory_order_acq_rel))
			{
				_error = ::std::move(error);
			}
		}

		auto stopped() noexcept -> void
		{
			auto expected = completion_kind::value;
			static_cast<void>(_kind.compare_exchange_strong(
				expected, completion_kind::stopped, ::std::memory_order_acq_rel));
		}

		auto arrive() noexcept -> void
		{
			if (_pending.fetch_sub(1, ::std::memory_order_acq_rel) != 1)
			{
				return;
			}

			_on_stop.reset();

			switch (_kind.load(::std::memory_order_acquire))
			{
			case completion_kind::value:
				::stdexec::set_value(::std::move(_receiver));
				return;
			case completion_kind::error:
				::stdexec::set_error(::std::move(_receiver), ::std::move(_error));
				return;
			case completion_kind::stopped:
				::stdexec::set_stopped(::std::move(_receiver));
				return;
			}
		}
	};

	::std::vector<ChildSender> _children;

	// 单发射：孩子只可移动，连接即消费。`let_value` 以右值连接它返回的 sender
	// （__let.hpp 里 `__nothrow_connectable<__sndr2_t, __rcvr2_t>` 用的是非引用类型），
	// `run_frame` 也是就地构造后立刻连，两处都够用。
	template <::stdexec::receiver Receiver>
	[[nodiscard]] auto connect(Receiver receiver) && -> operation<Receiver>
	{
		return operation<Receiver>{::std::move(_children), ::std::move(receiver)};
	}
};

template <class ChildSender>
[[nodiscard]] auto dynamic_when_all(::std::vector<ChildSender> children) -> dynamic_when_all_sender<ChildSender>
{
	return dynamic_when_all_sender<ChildSender>{._children = ::std::move(children)};
}

}
