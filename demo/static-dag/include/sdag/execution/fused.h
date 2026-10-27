#pragma once

/// 融合 op-state：图 sender 默认的连接结果，整张图连接后就是一个 operation state。
///
/// 布局（从上往下继承）：graph_state（值槽，也是取值源）→ fused_core（外层接收者、入度 / 汇点计数，需要时还有状态字、
/// 停止源、错误槽）→ fused_operation（各节点 op-state）。节点接收者只指向 fused_core，所以它在节点 op-state 算出来之前就完整。
///
/// 调度（全部编译期展开）：start 启动所有根；节点结束 -> node_done：汇点让汇点计数减一，最后一个到达时 finish；
/// 否则通知后继，入度 1 直接启动，入度 > 1 的计数减到 0 的那一方启动。整图已失败 / 停止时不启动，直接当作结束往下传。
/// 完成语义与 when_all 一致（第一个错误胜出，错误优先于停止）；不会发生的事不付费（对应成员是 0 字节）。
///
/// 生命周期：finish 完成外层接收者后 op-state 可能当场销毁，"可能触发 finish"之后不再碰 this（理由写在各循环旁）。
/// 常量求值止步于原子量与停止源，见 test/constexpr.cpp。

#include <atomic>
#include <cstddef>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <stdexec/execution.hpp>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/tuple.h"
#include "../general/sequence.h"
#include "../general/digraph.h"
#include "../graph/description.h"
#include "../graph/analysis.h"
#include "../graph/readers.h"
#include "../graph/storage_plan.h"
#include "../graph/completions.h"
#include "../graph/slots.h"
#include "../graph/visibility.h"

namespace sdag::execution
{

using ::std::size_t;

/// 完成状态：running → error | stopped。只从 running 出发：第一个错误或停止把状态切走，之后的都不再改变它，
/// 但停止之后来的错误仍要保存（错误优先于停止）。
enum class state_kind : unsigned char
{
	running,
	error,
	stopped,
};

/// 初值为 N 的原子倒计数（入度计数、汇点计数）。
template<size_t Initial>
struct counter
{
	::std::atomic<size_t> value{Initial};
};

/// std::variant<monostate, 错误类型...>；错误类型本身也可能是 monostate，所以一律按下标访问：错误 k 在第 k + 1 位。
template<class Errors>
using error_variant_t = general::rebind_t<::std::variant, general::concat_t<general::type_list<::std::monostate>, Errors>>;

/// 入度 > 1 的节点一个倒计数（初值是入度），其余节点 0 字节。
template<auto const& Shape, class Indices = ::std::make_index_sequence<Shape.count>>
struct pending;

template<auto const& Shape, size_t... I>
struct pending<Shape, ::std::index_sequence<I...>>
{
	using type = general::indexed_tuple<::std::conditional_t<(Shape.in_degree[I] > 1), counter<Shape.in_degree[I]>, general::empty>...>;
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

/// fused_core 要把自己 static_cast 成完整的 op-state 才能启动节点 op-state；完整类型在本文件末尾定义。
template<class Self, class Receiver>
struct fused_operation;

template<class Self, class Receiver>
struct fused_core : graph::graph_state<Self, ::stdexec::env_of_t<Receiver>>
{
	using env_type = ::stdexec::env_of_t<Receiver>;
	using state_base = graph::graph_state<Self, env_type>;
	using node_env_type = typename state_base::env_type;
	using analysis_type = typename state_base::analysis_type;
	using plan_type = typename state_base::plan_type;
	using completions_type = graph::graph_completions<Self, node_env_type>;
	using operation_type = fused_operation<Self, Receiver>;

	static constexpr auto const& shape = state_base::shape;
	static constexpr size_t sink_count = shape.sink_count();

	// 调度要展开的编译期列表：从形状 / 读者里取一次，固定成 std::array。
	static constexpr auto roots = general::to_array<shape.root_count()>(shape.roots());

	template<size_t J>
	static constexpr auto successors = general::to_array<shape.out_degree[J]>(shape.successors(J));

	/// 节点 J 结束时要析构的值：J move 的那些前驱（节点下标，升序）。
	template<size_t J>
	static constexpr auto released_by = []
	{
		constexpr auto const& taker = graph::readers_of<Self>().taker;
		constexpr size_t size = static_cast<size_t>(::std::ranges::count(taker, J));
		::std::array<size_t, size> result{};
		size_t cursor = 0;
		for (size_t node = 0; node < taker.size(); ++node)
		{
			if (taker[node] == J)
			{
				result[cursor++] = node;
			}
		}
		return result;
	}();

	// ------------------------------------------------------------ 这张图需要哪些状态

	/// 有节点可能失败 / 停止 / 存值时抛异常：需要状态字与停止源。
	static constexpr bool stoppable = state_base::stoppable;
	static constexpr bool sends_stopped = analysis_type::any_stopped;

	using error_types = typename completions_type::error_types;
	static constexpr bool has_errors = error_types::size != 0;
	using error_variant = error_variant_t<error_types>;

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

	Receiver _receiver;
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<stoppable, ::std::atomic<state_kind>, general::empty_for<fused_core, 0>> _state{};
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<stoppable, ::stdexec::inplace_stop_source, general::empty_for<fused_core, 1>> _stop_source{};
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<needs_callback, ::std::optional<stop_callback_t>, general::empty_for<fused_core, 2>> _on_stop{};
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<stoppable && has_errors, error_variant, general::empty_for<fused_core, 3>> _error{};
	SDAG_NO_UNIQUE_ADDRESS typename pending<shape>::type _pending{};
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<(sink_count > 1), counter<sink_count>, general::empty_for<fused_core, 4>> _sinks{};

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
		if constexpr (plan_type::forward[J])
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
				if (_state.load(::std::memory_order_acquire) == state_kind::stopped)
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
		else if constexpr (plan_type::slot_needed[J])
		{
			using tuple_type = general::rebind_t<::std::tuple, typename analysis_type::template values_t<J>>;
			static_assert(::std::is_constructible_v<tuple_type, Args&&...>, "sdag: 节点实际发出的值与它声明的完成签名不符");
			if constexpr (::std::is_nothrow_constructible_v<tuple_type, Args&&...>)
			{
				this->template slot_emplace<J>(::std::forward<Args>(args)...);
			}
			else
			{
				try
				{
					this->template slot_emplace<J>(::std::forward<Args>(args)...);
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
		switch (_state.exchange(state_kind::error, ::std::memory_order_acq_rel))
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
		stop_if_running();
		node_done<J>();
	}

	template<class Error>
	static constexpr size_t error_alternative = general::index_of_v<::std::decay_t<Error>, error_types> + 1;

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
		// J move 的值到此用完：先析构，再通知后继。其它读者都在 J 之前结束（编译期检查过），没人再碰它。
		general::for_each_value<released_by<J>>([this]<size_t From>() noexcept
		{
			this->template release<From>();
		});
		if constexpr (shape.is_sink(J))
		{
			sink_arrive();
		}
		else
		{
			// 通知后继可能启动它们，后继又可能同步完成整张图。但在通知完最后一个后继之前整图不可能结束：
			// 每个后继都是某个汇点的祖先，还没被通知的后继所在的那条路还没走完。所以循环里一直可以碰 this，
			// 最后一次通知之后不再碰。
			general::for_each_value<successors<J>>([this]<size_t To>() noexcept
			{
				this->template notify_edge<J, To>();
			});
		}
	}

	/// 析构节点 From 的值（它被 move 了，取走它的节点已经结束）。
	template<size_t From>
	constexpr void release() noexcept
	{
		if constexpr (plan_type::slot_needed[From])
		{
			this->template slot_reset<From>();
		}
	}

	template<size_t From, size_t To>
	constexpr void notify_edge() noexcept
	{
		if constexpr (shape.in_degree[To] == 1)
		{
			launch<To>();
		}
		else if (general::get<To>(_pending).value.fetch_sub(1, ::std::memory_order_acq_rel) == 1)
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
			if (_state.load(::std::memory_order_acquire) != state_kind::running)
			{
				node_done<J>();
				return;
			}
		}
		::stdexec::start(general::get<J>(static_cast<operation_type*>(this)->_operations));
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
			stop_if_running();
		}
		else
		{
			_stop_source.request_stop();
		}
	}

	/// 还在运行时切到 stopped 并请求停止；已经 error / stopped 时什么都不做。
	constexpr void stop_if_running() noexcept
	{
		auto expected = state_kind::running;
		if (_state.compare_exchange_strong(expected, state_kind::stopped, ::std::memory_order_acq_rel))
		{
			_stop_source.request_stop();
		}
	}

	/// 最后一个汇点到达：所有节点都已结束，按状态完成外层接收者。
	constexpr void finish() noexcept
	{
		if constexpr (stoppable)
		{
			// 先撤掉停止回调：撤回会等正在执行的回调结束，之后读到的状态就不会再变。
			if constexpr (needs_callback)
			{
				_on_stop.reset();
			}
			switch (_state.load(::std::memory_order_acquire))
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
		if constexpr (sink_count == 1 || !analysis_type::all_have_values)
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
	/// 汇点的值全部 move 给外层接收者（之后没人再读）。
	static constexpr auto sink_entries = []
	{
		::std::array<dependency_entry, sink_count> entries{};
		for (size_t index = 0; index < sink_count; ++index)
		{
			entries[index] = {shape.sinks()[index], mode::move};
		}
		return entries;
	}();
	static constexpr auto sink_table = graph::delivery_plan<Self, node_env_type, sink_entries>();

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
	using node_operation_t = ::stdexec::connect_result_t<graph::node_sender_t<Self, J>, node_receiver_t<J>>;

	static constexpr bool nothrow_connect =
		(noexcept(::stdexec::connect(::std::declval<graph::node_sender_t<Self, I>>(), ::std::declval<node_receiver_t<I>>())) && ...);

	/// 各节点的 op-state，由工厂直接构造在这里（op-state 不可移动）。
	using operations_t = general::indexed_tuple<node_operation_t<I>...>;
	operations_t _operations;

	constexpr explicit fused_operation_impl(Self self, Receiver receiver)
		noexcept(::std::is_nothrow_move_constructible_v<Receiver> && nothrow_connect)
		: core(::std::move(receiver))
		, _operations(general::from_factory,
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
		general::for_each_value<core::roots>([this]<size_t Root>() noexcept
		{
			this->template launch<Root>();
		});
	}
};

/// 整图 operation state。Self 是图 sender 的引用类型（G&& 或 G const&）。
template<class Self, class Receiver>
struct fused_operation : fused_operation_impl<Self, Receiver, ::std::make_index_sequence<graph::count_of_v<Self>>>
{
	using fused_operation_impl<Self, Receiver, ::std::make_index_sequence<graph::count_of_v<Self>>>::fused_operation_impl;
};

}
