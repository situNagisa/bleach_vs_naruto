#pragma once


#include <utility>
#include <atomic>
#include <span>
#include <ranges>
#include <algorithm>
#include <optional>

#include <stdexec/execution.hpp>

#include "./manual_lifetime.h"


enum class completion_kind : ::std::uint8_t
{
	value,
	error,
	stopped,
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
	struct operation_type
	{
		using receiver_type = ReceiverType;
		using operation_state_concept = ::stdexec::operation_state_t;

		struct child_receiver
		{
			using receiver_concept = ::stdexec::receiver_t;

			operation_type* _operation;

			constexpr auto set_value() const noexcept -> void { _operation->_arrive(); }
			constexpr auto set_error(::std::exception_ptr error) const noexcept -> void
			{
				_operation->_fail(::std::move(error));
				_operation->_arrive();
			}
			constexpr auto set_stopped() const noexcept -> void
			{
				_operation->_stopped();
				_operation->_arrive();
			}
			// 只覆盖 stop token：其余 query 仍走外层 env。内部 source 才能在兄弟失败时 request_stop。
			[[nodiscard]] constexpr auto get_env() const noexcept
			{
				return ::stdexec::env{
					::stdexec::prop{::stdexec::get_stop_token, _operation->_stop_source.get_token()},
					::stdexec::get_env(_operation->_receiver),
				};
			}
		};

		struct forward_stop
		{
			operation_type* _operation;

			auto operator()() const noexcept -> void
			{
				// 回调执行期间多握一枚计数，避免 last-arrive 在回调返回前拆掉 *this。
				_operation->_pending.fetch_add(1, ::std::memory_order_relaxed);
				_operation->_stopped();
				_operation->_arrive();
			}
		};

		using child_operation_type = ::stdexec::connect_result_t<_child_sender_type, child_receiver>;
		using outer_token_type = ::stdexec::stop_token_of_t<::stdexec::env_of_t<receiver_type>>;
		using stop_callback_type = typename outer_token_type::template callback_type<forward_stop>;

		receiver_type _receiver;
		::std::size_t _count;
		::std::unique_ptr<manual_lifetime<child_operation_type>[]> _operations;
		::std::atomic<::std::size_t> _pending;
		::std::atomic<completion_kind> _kind{ completion_kind::value };
		::std::exception_ptr _error{};
		::stdexec::inplace_stop_source _stop_source{};
		::std::optional<stop_callback_type> _on_stop{};

		constexpr operation_type(children_range_type sources, receiver_type receiver)
			: _receiver(::std::move(receiver))
			, _count(::std::ranges::size(sources))
			, _operations(::std::make_unique<manual_lifetime<child_operation_type>[]>(_count))
			, _pending(_count + 1)
		{
			for (auto&& [source, operation] : ::std::views::zip(::std::move(sources), ::std::span{ _operations.get(), _count }))
			{
				operation.construct([&] { return ::stdexec::connect(::std::move(source), child_receiver{ this }); });
			}
		}

		constexpr operation_type(operation_type const&) = delete;
		constexpr auto operator=(operation_type const&) -> operation_type & = delete;
		constexpr operation_type(operation_type&&) = delete;
		constexpr auto operator=(operation_type&&) -> operation_type & = delete;
		constexpr ~operation_type() noexcept = default;

		void start() & noexcept
		{
			if (_count == 0)
			{
				if (::stdexec::get_stop_token(::stdexec::get_env(_receiver)).stop_requested())
					::stdexec::set_stopped(::std::move(_receiver));
				else
					::stdexec::set_value(::std::move(_receiver));
				return;
			}
			_on_stop.emplace(::stdexec::get_stop_token(::stdexec::get_env(_receiver)), forward_stop{ this });
			::std::ranges::for_each(::std::span{ _operations.get(), _count }, ::stdexec::start, &manual_lifetime<child_operation_type>::get);
			_arrive();
		}
		auto _fail(::std::exception_ptr error) noexcept -> void
		{
			// error 覆盖 stopped：exchange 到 error 后，先前若是 value/stopped 都收下这个错误。
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

		auto _arrive() noexcept -> void
		{
			if (_pending.fetch_sub(1, ::std::memory_order_acq_rel) != 1)
				return;

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

	// 单发射：孩子只可移动，连接即消费。`let_value` 以右值连接它返回的 sender
	// （__let.hpp 里 `__nothrow_connectable<__sndr2_t, __rcvr2_t>` 用的是非引用类型），
	// `run_frame` 也是就地构造后立刻连，两处都够用。
	[[nodiscard]] auto connect(::stdexec::receiver auto receiver) && -> operation_type<decltype(receiver)>
	{
		static_assert(::stdexec::sender_in<_child_sender_type, ::stdexec::env_of_t<decltype(receiver)>>);
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

