#pragma once

/// 融合 op-state（默认连接方式）：整张图连接后是一个 operation state。
///
/// 扇出不走 split：每个节点的值存一份在整图 op-state 里，后继按引用读；
/// 扇入不走 when_all：入度 ≥ 2 的节点只有一个原子倒计数，入度 1 的节点由前驱直接启动。
/// 没有堆分配、没有引用计数、没有类型擦除；不会失败的图连停止源也不存在。
///
/// 完成语义与 when_all 一致：
///   - 全部成功：set_value(汇点 1 的值..., 汇点 2 的值..., ...)（汇点按声明顺序）
///   - 任一节点 set_error：请求停止、未启动的后继跳过、全部在飞节点结束后以第一个错误完成
///   - 任一节点 set_stopped：同上，以 set_stopped 完成（错误优先于停止）
///   - 值的传递：只有一个后继读值的前驱把值 move 给它；多个后继读值的前驱按 V const& 共享

#include <atomic>
#include <concepts>
#include <cstddef>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <stdexec/execution.hpp>

#include "meta.h"
#include "node.h"
#include "shape.h"
#include "graph_sender.h"
#include "graph.h"
#include "compute/primitives.h"
#include "compute/analysis.h"

namespace sdag::fused_detail
{

using ::std::size_t;
using graph_detail::node_sender_t;
using graph_detail::graph_state;

template<size_t Initial>
struct counter
{
	::std::atomic<size_t> value{Initial};
};

enum class state_kind : unsigned char
{
	running,
	error,
	stopped,
};

struct state_cell
{
	::std::atomic<state_kind> value{state_kind::running};
};

template<class Core, size_t I>
struct node_receiver
{
	using receiver_concept = ::stdexec::receiver_t;

	Core* _core;

	template<class... Args>
	void set_value(Args&&... args) noexcept
	{
		_core->template node_set_value<I>(::std::forward<Args>(args)...);
	}

	template<class Error>
	void set_error(Error&& error) noexcept
	{
		_core->template node_set_error<I>(::std::forward<Error>(error));
	}

	void set_stopped() noexcept
	{
		_core->template node_set_stopped<I>();
	}

	[[nodiscard]] typename Core::node_env_type get_env() const noexcept
	{
		return _core->make_node_env();
	}
};

template<auto Shape, class Indices = ::std::make_index_sequence<Shape.count>>
struct pending;

/// 入度 ≥ 2 的节点一个倒计数，其余不占空间。
template<auto Shape, size_t... I>
struct pending<Shape, ::std::index_sequence<I...>>
{
	using type = meta::indexed_tuple<::std::conditional_t<(Shape.in_degree[I] > 1), counter<Shape.in_degree[I]>, meta::empty>...>;
};

template<class Self, class Receiver>
struct fused_operation;

/// 除节点 op-state 之外的全部状态与调度逻辑。节点接收者只指向这一层，
/// 这样它的类型在节点 op-state 类型（定义在派生类里）算出来之前就是完整的。
template<class Self, class Receiver>
struct fused_core : graph_state<Self, ::stdexec::env_of_t<Receiver>>
{
	using env_type = ::stdexec::env_of_t<Receiver>;
	using state_base = graph_state<Self, env_type>;
	using node_env_type = typename state_base::env_type;
	using analysis_type = typename state_base::analysis_type;
	using operation_type = fused_operation<Self, Receiver>;

	static constexpr auto shape = state_base::shape;
	static constexpr size_t count = shape.count;
	static constexpr bool stoppable = state_base::stoppable;
	static constexpr bool sends_stopped = analysis_type::sends_stopped;
	static constexpr size_t sink_count = shape.sink_count();
	static constexpr bool direct_sink = analysis_type::direct_sink;
	static constexpr size_t sink_index = analysis_type::sink_index;

	using error_types = typename analysis_type::error_types;
	static constexpr bool has_errors = error_types::size != 0;
	/// std::variant<std::monostate, 错误类型...>。错误类型本身也可能是 std::monostate，所以访问一律按下标（错误 k 在第 k + 1 位）。
	using error_variant = compute::rebind_t<::std::variant, compute::concat_t<meta::type_list<::std::monostate>, error_types>>;

	struct forward_stop
	{
		fused_core* _core;

		void operator()() const noexcept
		{
			_core->on_outer_stop();
		}
	};

	using stop_token_t = ::stdexec::stop_token_of_t<env_type>;
	/// 外层令牌永远不会请求停止时，连回调槽也省掉。
	static constexpr bool needs_callback = stoppable && !::stdexec::unstoppable_token<stop_token_t>;
	using stop_callback_t = ::stdexec::stop_callback_for_t<stop_token_t, forward_stop>;

	struct state_tag;
	struct stop_source_tag;
	struct on_stop_tag;
	struct error_tag;
	struct sinks_tag;

	Receiver _receiver;
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<stoppable, state_cell, meta::empty_for<state_tag>> _state{};
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<stoppable, ::stdexec::inplace_stop_source, meta::empty_for<stop_source_tag>> _stop_source{};
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<needs_callback, ::std::optional<stop_callback_t>, meta::empty_for<on_stop_tag>> _on_stop{};
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<stoppable && has_errors, error_variant, meta::empty_for<error_tag>> _error{};
	SDAG_NO_UNIQUE_ADDRESS typename pending<shape>::type _pending{};
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<(sink_count > 1), counter<sink_count>, meta::empty_for<sinks_tag>> _sinks{};

	explicit fused_core(Receiver&& receiver) noexcept(::std::is_nothrow_move_constructible_v<Receiver>)
		: _receiver(::std::move(receiver))
	{
	}

	fused_core(fused_core&&) = delete;

	[[nodiscard]] node_env_type make_node_env() noexcept
	{
		if constexpr (stoppable)
		{
			return {this, _stop_source.get_token(), ::stdexec::get_env(_receiver)};
		}
		else
		{
			return {this, {}, ::stdexec::get_env(_receiver)};
		}
	}

	// ------------------------------------------------------------ 节点完成

	template<size_t J, class... Args>
	void node_set_value(Args&&... args) noexcept
	{
		if constexpr (direct_sink && J == sink_index)
		{
			// 唯一的汇点带值结束 = 整图结束：所有节点都是它的祖先，此刻全部已经结束。
			// 值不存槽位，直接转发；只有外层停止可能已经把状态切到 stopped。
			if constexpr (stoppable)
			{
				if constexpr (needs_callback)
				{
					_on_stop.reset();
				}
				if (_state.value.load(::std::memory_order_acquire) == state_kind::stopped)
				{
					if constexpr (sends_stopped)
					{
						::stdexec::set_stopped(::std::move(_receiver));
						return;
					}
					else
					{
						::std::unreachable();
					}
				}
			}
			::stdexec::set_value(::std::move(_receiver), ::std::forward<Args>(args)...);
			return;
		}
		else if constexpr (state_base::template slot_needed<J>)
		{
			using tuple_type = compute::rebind_t<::std::tuple, typename analysis_type::template values_t<J>>;
			static_assert(::std::is_constructible_v<tuple_type, Args&&...>, "sdag: 节点实际发出的值与它声明的完成签名不符");
			auto& slot = meta::get<J>(this->_slots);
			if constexpr (::std::is_nothrow_constructible_v<tuple_type, Args&&...>)
			{
				slot.emplace(::std::forward<Args>(args)...);
			}
			else
			{
				try
				{
					slot.emplace(::std::forward<Args>(args)...);
				}
				catch (...)
				{
					node_set_error<J>(::std::current_exception());
					return;
				}
			}
		}
		node_done<J>();
	}

	template<size_t J, class Error>
	void node_set_error(Error&& error) noexcept
	{
		static_assert(stoppable && has_errors);
		switch (_state.value.exchange(state_kind::error, ::std::memory_order_acq_rel))
		{
		case state_kind::running:
			_stop_source.request_stop();
			[[fallthrough]];
		case state_kind::stopped:
			// 第一个错误；之后的错误全部丢弃。
			store_error(::std::forward<Error>(error));
			break;
		case state_kind::error:
			break;
		}
		node_done<J>();
	}

	template<size_t J>
	void node_set_stopped() noexcept
	{
		static_assert(stoppable && sends_stopped);
		auto expected = state_kind::running;
		if (_state.value.compare_exchange_strong(expected, state_kind::stopped, ::std::memory_order_acq_rel))
		{
			_stop_source.request_stop();
		}
		node_done<J>();
	}

	/// 错误类型在 error_types 里的位置 + 1 = 它在 _error 里的下标。
	template<class Error>
	static constexpr size_t error_alternative = compute::index_of_v<::std::decay_t<Error>, error_types> + 1;

	template<class Error>
	void store_error(Error&& error) noexcept
	{
		if constexpr (::std::is_nothrow_constructible_v<::std::decay_t<Error>, Error>)
		{
			_error.template emplace<error_alternative<Error>>(::std::forward<Error>(error));
		}
		else
		{
			try
			{
				_error.template emplace<error_alternative<Error>>(::std::forward<Error>(error));
			}
			catch (...)
			{
				_error.template emplace<error_alternative<::std::exception_ptr>>(::std::current_exception());
			}
		}
	}

	// ------------------------------------------------------------ 调度

	/// 节点 J 结束（成功、失败、停止或被跳过）：是汇点就向整图报到，否则通知后继。
	template<size_t J>
	void node_done() noexcept
	{
		if constexpr (shape.is_sink(J))
		{
			sink_arrive();
		}
		else
		{
			// 最后一个后继被通知之前整图不可能结束（每个后继都是某个汇点的祖先），所以循环里一直可以碰 this。
			compute::for_each_value<successors_v<shape, J>>([this]<size_t To>() noexcept
			{
				this->template notify_edge<J, To>();
			});
		}
	}

	template<size_t From, size_t To>
	void notify_edge() noexcept
	{
		if constexpr (shape.in_degree[To] == 1)
		{
			launch<To>();
		}
		else if (meta::get<To>(_pending).value.fetch_sub(1, ::std::memory_order_acq_rel) == 1)
		{
			launch<To>();
		}
	}

	/// 前驱全部到齐。图已经失败 / 停止时不再启动，直接当作结束往下传。
	template<size_t J>
	void launch() noexcept
	{
		if constexpr (stoppable)
		{
			if (_state.value.load(::std::memory_order_acquire) != state_kind::running)
			{
				node_done<J>();
				return;
			}
		}
		::stdexec::start(meta::get<J>(static_cast<operation_type*>(this)->_operations));
	}

	void sink_arrive() noexcept
	{
		if constexpr (sink_count > 1)
		{
			if (_sinks.value.fetch_sub(1, ::std::memory_order_acq_rel) != 1)
			{
				return;
			}
		}
		finish();
	}

	/// 外层停止请求。图能以 stopped 完成时切换状态（后继跳过）；
	/// 否则只把请求转给节点，图仍以它们的值完成——与 when_all 一致。
	/// request_stop 可能同步完成整图并销毁 this（inplace_stop_source 自己处理"回调里被销毁"），之后不再碰 this。
	void on_outer_stop() noexcept
	{
		if constexpr (sends_stopped)
		{
			auto expected = state_kind::running;
			if (_state.value.compare_exchange_strong(expected, state_kind::stopped, ::std::memory_order_acq_rel))
			{
				_stop_source.request_stop();
			}
		}
		else
		{
			_stop_source.request_stop();
		}
	}

	void finish() noexcept
	{
		if constexpr (stoppable)
		{
			if constexpr (needs_callback)
			{
				_on_stop.reset();
			}
			switch (_state.value.load(::std::memory_order_acquire))
			{
			case state_kind::error:
				if constexpr (has_errors)
				{
					emit_error();
					return;
				}
				else
				{
					::std::unreachable();
				}
			case state_kind::stopped:
				if constexpr (sends_stopped)
				{
					::stdexec::set_stopped(::std::move(_receiver));
					return;
				}
				else
				{
					::std::unreachable();
				}
			case state_kind::running:
				break;
			}
		}
		if constexpr (direct_sink || !analysis_type::has_values)
		{
			// 唯一汇点的值在 node_set_value 里直接转发；没有值完成的图也不会以 running 状态走到这里。
			::std::unreachable();
		}
		else
		{
			emit_values(::std::make_index_sequence<sink_value_count>{});
		}
	}

	void emit_error() noexcept
	{
		// set_error 之后 op-state 可能已经销毁：用短路的 || 保证命中之后不再读 _error。
		[this]<size_t... K>(::std::index_sequence<K...>)
		{
			(void)((_error.index() == K + 1
				? (::stdexec::set_error(::std::move(_receiver), ::std::get<K + 1>(::std::move(_error))), true)
				: false) || ...);
		}(::std::make_index_sequence<error_types::size>{});
	}

	/// 各汇点的值按声明顺序铺平，全部 move 给整图的接收者。
	static constexpr auto sink_table = graph_detail::delivery_plan<Self, node_env_type, sinks_v<shape>>(false);
	static constexpr size_t sink_value_count = sink_table.size();

	template<size_t... E>
	void emit_values(::std::index_sequence<E...>) noexcept
	{
		::stdexec::set_value(::std::move(_receiver), this->template access<sink_table[E]>()...);
	}
};

template<class Self, class Receiver, class Indices>
struct fused_operation_impl;

template<class Self, class Receiver, size_t... I>
struct fused_operation_impl<Self, Receiver, ::std::index_sequence<I...>> : fused_core<Self, Receiver>
{
	using core = fused_core<Self, Receiver>;

	template<size_t J>
	using node_receiver_t = node_receiver<core, J>;

	/// 子 sender 按 Self 的值类别取出：右值图 move 子 sender，const& 图拷贝它们。
	template<size_t J>
	using node_operation_t = ::stdexec::connect_result_t<node_sender_t<Self, J>, node_receiver_t<J>>;

	using operations_t = meta::indexed_tuple<node_operation_t<I>...>;

	static constexpr bool nothrow_connect =
		(noexcept(::stdexec::connect(::std::declval<node_sender_t<Self, I>>(), ::std::declval<node_receiver_t<I>>())) && ...);

	operations_t _operations;

	explicit fused_operation_impl(Self self, Receiver receiver)
		noexcept(::std::is_nothrow_move_constructible_v<Receiver> && nothrow_connect)
		: core(::std::move(receiver))
		, _operations(meta::from_factory,
			[&]
			{
				// 每个工厂只从图 sender 里取自己那一个子 sender（第 I + 2 个元素），右值 Self 下也不会重复移动。
				return ::stdexec::connect(::std::forward<Self>(self).template get<I + 2>(), node_receiver_t<I>{this});
			}...)
	{
	}

	void start() noexcept
	{
		if constexpr (core::count == 0)
		{
			::stdexec::set_value(::std::move(this->_receiver));
			return;
		}
		if constexpr (core::needs_callback)
		{
			this->_on_stop.emplace(::stdexec::get_stop_token(::stdexec::get_env(this->_receiver)), typename core::forward_stop{this});
		}
		// 最后一个根启动之前整图不可能结束（每个汇点都有某个根是祖先，而整图要等所有汇点）。
		compute::for_each_value<roots_v<core::shape>>([this]<size_t Root>() noexcept
		{
			this->template launch<Root>();
		});
	}
};

/// 整图 operation state；Self 是图 sender 的引用类型（X&& 或 X const&）。
template<class Self, class Receiver>
struct fused_operation : fused_operation_impl<Self, Receiver, ::std::make_index_sequence<graph_detail::count_of_v<Self>>>
{
	using fused_operation_impl<Self, Receiver, ::std::make_index_sequence<graph_detail::count_of_v<Self>>>::fused_operation_impl;
};

}

namespace sdag
{

/// 默认策略：图 sender 本身（连接时融合成一个 op-state）。
struct fused
{
	template<graph_node... Nodes>
	[[nodiscard]] static constexpr auto compose(Nodes&&... nodes)
	{
		return dag_t{}(::std::forward<Nodes>(nodes)...);
	}
};

}
