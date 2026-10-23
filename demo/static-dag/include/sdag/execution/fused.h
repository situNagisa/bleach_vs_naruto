#pragma once

/// 融合 op-state：图 sender 默认的连接结果，整张图连接后就是一个 operation state。
///
/// 布局（fused_operation 从上往下继承）：
///   graph_state     节点的值槽（node_input.h）。节点环境里只暴露这一层。
///   fused_core      调度状态与逻辑：外层接收者、入度计数、汇点计数，以及（需要时）状态字、停止源、错误槽。
///   fused_operation 各节点的 op-state。节点接收者只指向 fused_core，所以接收者的类型在节点 op-state 的类型
///                   算出来之前就是完整的——否则"节点 op-state 依赖接收者、接收者依赖 op-state"会成环。
///
/// 调度（全部是编译期展开，没有运行期的图结构）：
///   start：启动所有根。
///   节点 J 结束（成功、失败、停止、或被跳过）-> node_done<J>：
///     J 是汇点：汇点计数减一，最后一个汇点到达时 finish。
///     否则通知每个后继：入度 1 的后继直接启动；入度 > 1 的后继把它的入度计数减一，减到 0 的那一方启动它。
///   启动之前若整图已经失败 / 停止，不启动，直接当作"结束"往下传（node_done），这样计数照样归零、整图照样结束。
///   扇出不需要 split：值存在整图里，后继按引用读。扇入不需要 when_all：一个原子计数就够了。
///
/// 完成语义与 when_all 一致：
///   全部成功：set_value(汇点 1 的值..., 汇点 2 的值..., ...)，汇点按声明顺序；
///   任一节点 set_error：状态切到 error 并请求停止，还没启动的节点跳过，在飞的节点结束后以第一个错误完成；
///   任一节点 set_stopped：同上，以 set_stopped 完成；错误优先于停止。
///
/// 只为会发生的事付费：图里没有节点会失败 / 停止 / 存值时抛异常，就没有状态字、停止源、错误槽（0 字节）；
/// 外层令牌永远不会请求停止，就没有停止回调；只有一个汇点就没有汇点计数，它的值也不存槽位、直接转发；入度 1 的节点没有入度计数。
///
/// 生命周期：整图在 finish 里完成外层接收者，接收者可能当场销毁整个 op-state。所以一个函数"可能触发 finish"之后
/// 就不能再碰 this。几处遍历（通知后继、启动根）都能保证这一点，原因写在各自的循环旁边。
///
/// 常量求值：成员函数都标了 constexpr，常量求值止步于原子量与停止源——入度计数、汇点计数、状态字是 std::atomic，
/// 停止源是 inplace_stop_source，都不能常量求值。能在 static_assert 里跑完的只有不碰它们的图：只有一个汇点、
/// 没有入度 > 1 的节点、不会失败（例如一条 noexcept 的链），见 test/constexpr.cpp。

#include <atomic>
#include <cstddef>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <stdexec/execution.hpp>

#include "../meta_program/type_list.h"
#include "../meta_program/utility.h"
#include "../meta_program/indexed_tuple.h"
#include "../fwd.h"
#include "../graph/shape.h"
#include "../graph/traits.h"
#include "../execution/node_input.h"
#include "../meta_program/index_of.h"
#include "../meta_program/concat.h"
#include "../meta_program/rebind.h"
#include "../meta_program/for_each_value.h"
#include "../graph/analyze/graph_analysis.h"

namespace sdag::fused_detail
{

using ::std::size_t;

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

/// 入度 > 1 的节点一个倒计数（初值是入度），其余节点 0 字节。
template<auto Shape, class Indices = ::std::make_index_sequence<Shape.count>>
struct pending;

template<auto Shape, size_t... I>
struct pending<Shape, ::std::index_sequence<I...>>
{
	using type = meta::indexed_tuple<::std::conditional_t<(Shape.in_degree[I] > 1), counter<Shape.in_degree[I]>, meta::empty>...>;
};

/// 节点 I 的接收者：把完成转给 fused_core，环境是节点环境。
template<class Core, size_t I>
struct node_receiver
{
	using receiver_concept = ::stdexec::receiver_t;

	Core* _core;

	template<class... Args>
	constexpr void set_value(Args&&... args) noexcept
	{
		_core->template node_set_value<I>(::std::forward<Args>(args)...);
	}

	template<class Error>
	constexpr void set_error(Error&& error) noexcept
	{
		_core->template node_set_error<I>(::std::forward<Error>(error));
	}

	constexpr void set_stopped() noexcept
	{
		_core->template node_set_stopped<I>();
	}

	[[nodiscard]] constexpr typename Core::node_env_type get_env() const noexcept
	{
		return _core->make_node_env();
	}
};

template<class Self, class Receiver>
struct fused_core : graph_detail::graph_state<Self, ::stdexec::env_of_t<Receiver>>
{
	using env_type = ::stdexec::env_of_t<Receiver>;
	using state_base = graph_detail::graph_state<Self, env_type>;
	using node_env_type = typename state_base::env_type;
	using analysis_type = typename state_base::analysis_type;
	using operation_type = fused_operation<Self, Receiver>;

	static constexpr auto shape = state_base::shape;
	static constexpr size_t sink_count = shape.sink_count();

	// ------------------------------------------------------------ 这张图需要哪些状态

	/// 有节点可能失败 / 停止 / 存值时抛异常：需要状态字与停止源。
	static constexpr bool stoppable = state_base::stoppable;
	static constexpr bool sends_stopped = analysis_type::sends_stopped;
	/// 只有一个汇点：它的值不存槽位，在 node_set_value 里直接转发给外层接收者。
	static constexpr bool direct_sink = analysis_type::direct_sink;
	static constexpr size_t sink_index = analysis_type::sink_index;

	using error_types = typename analysis_type::error_types;
	static constexpr bool has_errors = error_types::size != 0;
	/// std::variant<std::monostate, 错误类型...>。错误类型本身也可能是 std::monostate，所以一律按下标访问：错误 k 在第 k + 1 位。
	using error_variant = meta::rebind_t<::std::variant, meta::concat_t<meta::type_list<::std::monostate>, error_types>>;

	/// 外层停止令牌上挂的回调：把外层的停止请求转进整图。
	struct forward_stop
	{
		fused_core* _core;

		constexpr void operator()() const noexcept
		{
			_core->on_outer_stop();
		}
	};

	using stop_token_t = ::stdexec::stop_token_of_t<env_type>;
	/// 外层令牌永远不会请求停止时，连回调槽也省掉。
	static constexpr bool needs_callback = stoppable && !::stdexec::unstoppable_token<stop_token_t>;
	using stop_callback_t = ::stdexec::stop_callback_for_t<stop_token_t, forward_stop>;

	// ------------------------------------------------------------ 成员（不需要的都是 0 字节）

	/// 用不上的成员各占一个互不相同的空类型，no_unique_address 才能把它们都压成 0 字节。
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

	constexpr explicit fused_core(Receiver&& receiver) noexcept(::std::is_nothrow_move_constructible_v<Receiver>)
		: _receiver(::std::move(receiver))
	{
	}

	fused_core(fused_core&&) = delete;

	[[nodiscard]] constexpr node_env_type make_node_env() noexcept
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
	constexpr void node_set_value(Args&&... args) noexcept
	{
		if constexpr (direct_sink && J == sink_index)
		{
			// 唯一的汇点带值结束 = 整图结束：所有节点都是它的祖先，此刻已经全部结束。
			// 值不存槽位，直接转发。这时状态不可能是 error（失败的话这个汇点根本不会被启动），
			// 只可能被外层停止切到了 stopped。
			if constexpr (stoppable)
			{
				// 先撤掉停止回调：撤回会等正在执行的回调结束，之后读到的状态就不会再变。
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
		else if constexpr (state_base::analysis_type::slot_needed[J])
		{
			using tuple_type = meta::rebind_t<::std::tuple, typename analysis_type::template values_t<J>>;
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
					// 存值失败当作这个节点以 exception_ptr 失败（node_set_error 里会调 node_done）。
					node_set_error<J>(::std::current_exception());
					return;
				}
			}
		}
		node_done<J>();
	}

	template<size_t J, class Error>
	constexpr void node_set_error(Error&& error) noexcept
	{
		static_assert(stoppable && has_errors);
		switch (_state.value.exchange(state_kind::error, ::std::memory_order_acq_rel))
		{
		case state_kind::running:
			_stop_source.request_stop();
			[[fallthrough]];
		case state_kind::stopped:
			// 第一个错误才保存；之后的错误丢弃。停止之后来的错误也要保存：错误优先于停止。
			store_error(::std::forward<Error>(error));
			break;
		case state_kind::error:
			break;
		}
		node_done<J>();
	}

	template<size_t J>
	constexpr void node_set_stopped() noexcept
	{
		static_assert(stoppable && sends_stopped);
		auto expected = state_kind::running;
		if (_state.value.compare_exchange_strong(expected, state_kind::stopped, ::std::memory_order_acq_rel))
		{
			_stop_source.request_stop();
		}
		node_done<J>();
	}

	template<class Error>
	static constexpr size_t error_alternative = meta::index_of_v<::std::decay_t<Error>, error_types> + 1;

	template<class Error>
	constexpr void store_error(Error&& error) noexcept
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

	/// 节点 J 结束（成功、失败、停止或被跳过）。
	template<size_t J>
	constexpr void node_done() noexcept
	{
		if constexpr (shape.is_sink(J))
		{
			sink_arrive();
		}
		else
		{
			// 通知后继可能启动它们，后继又可能同步完成整张图。但在通知完最后一个后继之前整图不可能结束：
			// 每个后继都是某个汇点的祖先，还没被通知的后继所在的那条路还没走完。所以循环里一直可以碰 this，
			// 最后一次通知之后不再碰。
			meta::for_each_value<successors_v<shape, J>>([this]<size_t To>() noexcept
			{
				this->template notify_edge<J, To>();
			});
		}
	}

	template<size_t From, size_t To>
	constexpr void notify_edge() noexcept
	{
		if constexpr (shape.in_degree[To] == 1)
		{
			launch<To>();
		}
		else if (meta::get<To>(_pending).value.fetch_sub(1, ::std::memory_order_acq_rel) == 1)
		{
			// 最后一个到达的前驱启动它。acq_rel 让这个线程看得到其它前驱存进槽位的值。
			launch<To>();
		}
	}

	/// 节点 J 的前驱全部到齐。整图已经失败 / 停止时不启动，直接当作结束往下传。
	template<size_t J>
	constexpr void launch() noexcept
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

	constexpr void sink_arrive() noexcept
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

	/// 外层请求停止。图能以 stopped 完成时把状态切到 stopped（还没启动的节点跳过）；
	/// 不能时只把请求转给节点，整图仍以它们的结果完成——与 when_all 一致。
	/// request_stop 可能同步完成整图并销毁 this（inplace_stop_source 自己处理"回调里 source 被销毁"），之后不再碰 this。
	constexpr void on_outer_stop() noexcept
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

	/// 最后一个汇点到达：所有节点都已结束，按状态完成外层接收者。
	constexpr void finish() noexcept
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
			// 唯一汇点的值在 node_set_value 里已经直接转发了；没有值完成的图也不会以 running 状态走到这里。
			::std::unreachable();
		}
		else
		{
			emit_values(::std::make_index_sequence<sink_table.size()>{});
		}
	}

	constexpr void emit_error() noexcept
	{
		// set_error 之后 op-state 可能已经销毁：用短路的 || 保证命中那一项之后不再读 _error。
		[this]<size_t... K>(::std::index_sequence<K...>)
		{
			(void)((_error.index() == K + 1
				? (::stdexec::set_error(::std::move(_receiver), ::std::get<K + 1>(::std::move(_error))), true)
				: false) || ...);
		}(::std::make_index_sequence<error_types::size>{});
	}

	/// 各汇点的值按声明顺序铺平，全部 move 给外层接收者。
	static constexpr auto sink_table = graph_detail::delivery_plan<Self, node_env_type, sinks_v<shape>>(false);

	template<size_t... E>
	constexpr void emit_values(::std::index_sequence<E...>) noexcept
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

	template<size_t J>
	using node_operation_t = ::stdexec::connect_result_t<graph_detail::node_sender_t<Self, J>, node_receiver_t<J>>;

	static constexpr bool nothrow_connect =
		(noexcept(::stdexec::connect(::std::declval<graph_detail::node_sender_t<Self, I>>(), ::std::declval<node_receiver_t<I>>())) && ...);

	/// 各节点的 op-state，由工厂直接构造在这里（op-state 不可移动）。
	using operations_t = meta::indexed_tuple<node_operation_t<I>...>;
	operations_t _operations;

	constexpr explicit fused_operation_impl(Self self, Receiver receiver)
		noexcept(::std::is_nothrow_move_constructible_v<Receiver> && nothrow_connect)
		: core(::std::move(receiver))
		, _operations(meta::from_factory,
			[&]
			{
				// 每个工厂只从图 sender 里取自己那一个节点 sender（第 I + 2 个元素），右值 Self 也不会被重复移动。
				return ::stdexec::connect(::std::forward<Self>(self).template get<I + 2>(), node_receiver_t<I>{this});
			}...)
	{
	}

	constexpr void start() noexcept
	{
		if constexpr (core::shape.count == 0)
		{
			::stdexec::set_value(::std::move(this->_receiver));
			return;
		}
		if constexpr (core::needs_callback)
		{
			this->_on_stop.emplace(::stdexec::get_stop_token(::stdexec::get_env(this->_receiver)), typename core::forward_stop{this});
		}
		// 启动完最后一个根之前整图不可能结束：整图要等所有汇点，而还没启动的根下面的汇点还没到。
		meta::for_each_value<roots_v<core::shape>>([this]<size_t Root>() noexcept
		{
			this->template launch<Root>();
		});
	}
};

/// 整图 operation state。Self 是图 sender 的引用类型（G&& 或 G const&）。
template<class Self, class Receiver>
struct fused_operation : fused_operation_impl<Self, Receiver, ::std::make_index_sequence<graph_detail::count_of_v<Self>>>
{
	using fused_operation_impl<Self, Receiver, ::std::make_index_sequence<graph_detail::count_of_v<Self>>>::fused_operation_impl;
};

}
