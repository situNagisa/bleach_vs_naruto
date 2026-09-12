#pragma once


#include <utility>
#include <atomic>
#include <span>
#include <ranges>
#include <algorithm>
#include <optional>
#include <memory>
#include <exception>
#include <cstdint>
#include <cstddef>

#include <stdexec/execution.hpp>

#include "./manual_lifetime.h"


enum class completion_kind : ::std::uint8_t
{
	value,
	error,
	stopped,
};

/// 汇合点的可变状态。**先于任何嵌套类型定义完**，这样 `child_receiver` 的成员函数体
/// 和推导返回类型都能安全引用它——上一版把它们塞在 `operation_type` 里，
/// `child_operation_type` 这个 alias 在类还没闭合时就要求实例化 `get_env()`，两个编译器都拒。
template <class receiver_type>
struct dynamic_when_all_state
{
	receiver_type _receiver;
	::std::size_t _count;
	::std::atomic<::std::size_t> _pending;
	::std::atomic<completion_kind> _kind{ completion_kind::value };
	::std::exception_ptr _error{};
	::stdexec::inplace_stop_source _stop_source{};

	auto _fail(::std::exception_ptr error) noexcept -> void
	{
		switch (_kind.exchange(completion_kind::error, ::std::memory_order_acq_rel))
		{
		case completion_kind::value:
			_stop_source.request_stop();
			[[fallthrough]];
		case completion_kind::stopped:
			_error = ::std::move(error);
			break;
		case completion_kind::error:
			break;
		}
	}

	auto _stopped() noexcept -> void
	{
		auto expected = completion_kind::value;
		if (_kind.compare_exchange_strong(expected, completion_kind::stopped, ::std::memory_order_acq_rel))
			_stop_source.request_stop();
	}
};

template <class receiver_type>
struct dynamic_when_all_child_receiver
{
	using receiver_concept = ::stdexec::receiver_t;

	dynamic_when_all_state<receiver_type>* _state;
	void (*_arrive)(dynamic_when_all_state<receiver_type>*) noexcept;

	constexpr auto set_value() const noexcept -> void { _arrive(_state); }
	constexpr auto set_error(::std::exception_ptr error) const noexcept -> void
	{
		_state->_fail(::std::move(error));
		_arrive(_state);
	}
	constexpr auto set_stopped() const noexcept -> void
	{
		_state->_stopped();
		_arrive(_state);
	}
	[[nodiscard]] constexpr auto get_env() const noexcept
	{
		return ::stdexec::env{
			::stdexec::prop{::stdexec::get_stop_token, _state->_stop_source.get_token()},
			::stdexec::get_env(_state->_receiver),
		};
	}
};

template <::std::ranges::sized_range ChildrenRange>
	requires ::std::move_constructible<ChildrenRange> && ::stdexec::sender<::std::ranges::range_value_t<ChildrenRange>>
struct dynamic_when_all_sender
{
	using children_range_type = ChildrenRange;
	using sender_concept = ::stdexec::sender_t;
	using completion_signatures = ::stdexec::completion_signatures<
		::stdexec::set_value_t(),
		::stdexec::set_error_t(::std::exception_ptr),
		::stdexec::set_stopped_t()>;

	using _child_sender_type = ::std::ranges::range_value_t<children_range_type>;

	children_range_type _children;

	template <class ReceiverType>
	struct operation_type : dynamic_when_all_state<ReceiverType>
	{
		using receiver_type = ReceiverType;
		using operation_state_concept = ::stdexec::operation_state_t;
		using state_type = dynamic_when_all_state<ReceiverType>;
		using child_receiver = dynamic_when_all_child_receiver<ReceiverType>;

		struct forward_stop
		{
			operation_type* _operation;

			auto operator()() const noexcept -> void
			{
				_operation->_pending.fetch_add(1, ::std::memory_order_relaxed);
				_operation->_stopped();
				_operation->_arrive();
			}
		};

		using child_operation_type = ::stdexec::connect_result_t<_child_sender_type, child_receiver>;
		using outer_token_type = ::stdexec::stop_token_of_t<::stdexec::env_of_t<receiver_type>>;
		using stop_callback_type = typename outer_token_type::template callback_type<forward_stop>;

		::std::unique_ptr<manual_lifetime<child_operation_type>[]> _operations;
		::std::optional<stop_callback_type> _on_stop{};

		static auto _arrive_thunk(state_type* state) noexcept -> void
		{
			static_cast<operation_type*>(state)->_arrive();
		}

		constexpr operation_type(children_range_type sources, receiver_type receiver)
			: state_type{
				._receiver = ::std::move(receiver),
				._count = ::std::ranges::size(sources),
				._pending = ::std::ranges::size(sources) + 1 }
			, _operations(::std::make_unique<manual_lifetime<child_operation_type>[]>(this->_count))
		{
			for (auto&& [source, operation] : ::std::views::zip(::std::move(sources), ::std::span{ _operations.get(), this->_count }))
			{
				operation.construct([&] {
					return ::stdexec::connect(::std::move(source), child_receiver{ this, &_arrive_thunk });
				});
			}
		}

		constexpr operation_type(operation_type const&) = delete;
		constexpr auto operator=(operation_type const&) -> operation_type & = delete;
		constexpr operation_type(operation_type&&) = delete;
		constexpr auto operator=(operation_type&&) -> operation_type & = delete;
		constexpr ~operation_type() noexcept = default;

		void start() & noexcept
		{
			if (this->_count == 0)
			{
				if (::stdexec::get_stop_token(::stdexec::get_env(this->_receiver)).stop_requested())
					::stdexec::set_stopped(::std::move(this->_receiver));
				else
					::stdexec::set_value(::std::move(this->_receiver));
				return;
			}
			_on_stop.emplace(::stdexec::get_stop_token(::stdexec::get_env(this->_receiver)), forward_stop{ this });
			::std::ranges::for_each(::std::span{ _operations.get(), this->_count }, ::stdexec::start, &manual_lifetime<child_operation_type>::get);
			_arrive();
		}

		auto _arrive() noexcept -> void
		{
			if (this->_pending.fetch_sub(1, ::std::memory_order_acq_rel) != 1)
				return;

			_on_stop.reset();

			switch (this->_kind.load(::std::memory_order_acquire))
			{
			case completion_kind::value:
				::stdexec::set_value(::std::move(this->_receiver));
				return;
			case completion_kind::error:
				::stdexec::set_error(::std::move(this->_receiver), ::std::move(this->_error));
				return;
			case completion_kind::stopped:
				::stdexec::set_stopped(::std::move(this->_receiver));
				return;
			}
		}
	};

	[[nodiscard]] auto connect(::stdexec::receiver auto receiver) && -> operation_type<decltype(receiver)>
	{
		return operation_type<decltype(receiver)>{::std::move(_children), ::std::move(receiver)};
	}
};

struct dynamic_when_all_t
{
	[[nodiscard]] constexpr auto operator()(::std::ranges::sized_range auto children) const
		-> dynamic_when_all_sender<decltype(children)>
		requires ::std::move_constructible<decltype(children)>
			&& ::stdexec::sender<::std::ranges::range_value_t<decltype(children)>>
	{
		return dynamic_when_all_sender<decltype(children)>{._children = ::std::move(children)};
	}
};

inline constexpr dynamic_when_all_t dynamic_when_all{};
