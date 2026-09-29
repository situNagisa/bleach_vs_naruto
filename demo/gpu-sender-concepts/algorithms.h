#pragma once

#include <optional>
#include <utility>

#include <stdexec/execution.hpp>

#include "./concepts.h"

/// 三个派生算法：transition / submit / consume。
///
/// 这三个算法都不认识任何具体的 domain/pending_value/scheduler 类型（不
/// `#include "./tracked_value.h"` 或 "./vulkan_tracked_value.h"）——它们只用
/// concepts.h 里的 tracked_resource/transitible_tracked_resource/
/// pending_resource 这几个 concept，以及 cpo.h 里的 transite/consume_external
/// 两个原语。
///
/// transition/consume 是"定制点"，不是把 transite/consume_external 原语原样
/// 包一层：它们在 connect 阶段用标准的
/// stdexec::get_completion_scheduler<set_value_t> 查询查出 predecessor 的
/// completion scheduler（这是标准 stdexec 机制，不是我们自定义的窄查询——
/// 调查过 nvexec 的 stream_scheduler 用的就是这一套）；如果这个 scheduler
/// 类型提供 transition_impl/consume_impl 成员（域自己的定制实现，比如
/// gpu::mock::mock_scheduler、gpu::vk::vk_scheduler 各自提供的那份），就调用
/// 它；查不到 scheduler，或者 scheduler 没提供定制点，就走默认行为——
/// transition 默认只调用 transite（纯代数变换，不做任何 GPU 调用），consume
/// 默认调用 consume_external 原语本身（它自己内部决定"没有域"时怎么办，比如
/// mock 版本会真的等信号）。
///
/// submit 不一样：它是提交边界，Domain 是显式传入的引用（不是查出来的），
/// 提交之后这条链就离开了域——所以 submit 之后如果还要 consume，走的是
/// "没有 scheduler 可查"这条默认路径，这是有意的行为，不是遗漏。
namespace gpu
{
namespace detail
{
	// std::optional<T>::emplace(args...) 要求 is_constructible_v<T, Args...>——
	// 对于像 stdexec::connect_result_t<...> 这类不可移动/不可拷贝的类型（内部
	// opstate 用 STDEXEC_IMMOVABLE 声明），不能先调用 stdexec::connect(...) 拿到
	// 一个值再传给 emplace，那等于要求"用一个 T 值构造 T"，而 T 恰恰不可移动。
	// 用一个只有隐式转换到 T 的包装类型让 emplace 走隐式转换构造这条路：
	// emplace 内部对包装类型调用 T 的转换构造函数，转换函数体里才真正调用
	// fn()，是同一个直接初始化表达式的一部分，没有中间的"先有一个 T 值"这一步。
	// stdexec 自己的 __emplace_from（__detail/__utility.hpp）就是同一个手法，
	// 用在 let_value/finally/sequence 等需要就地 connect 出不可移动 opstate 的
	// 场景——这里是同一模式在 std::optional 上的等价写法。
	template <class Fn>
	struct emplace_from
	{
		Fn _fn;
		using __t = decltype(_fn());
		operator __t() && { return static_cast<Fn&&>(_fn)(); }
	};
	template <class Fn> emplace_from(Fn) -> emplace_from<Fn>;
}
namespace make_pending_cpo
{
	void make_pending();

	enum class choose { adl, none };
	struct choice_result { choose strategy; bool nothrow; };

	// 跟 cpo.h 里的四个原语同一套写法：choice 用 auto&&/forward 转发真实实参，
	// 不用 declval——两者语义不等价，见 cpo.h 顶部和
	// skills/modern-cpp-concept-design/references/CPO(custom point object).md
	// 里对这个问题的完整说明。
	consteval choice_result choice(auto&& item, auto&& signal) noexcept
	{
		if constexpr (requires { make_pending(::std::forward<decltype(item)>(item), ::std::forward<decltype(signal)>(signal)); })
			return {choose::adl, noexcept(make_pending(::std::forward<decltype(item)>(item), ::std::forward<decltype(signal)>(signal)))};
		else
			return {choose::none, true};
	}
}

/// make_pending(item, signal)：把一个刚 submit 完的 tracked_resource 和这批提交
/// 产出的信号，包成对应 domain 那一套的 pending_value。ADL 到具体 domain 的命名
/// 空间——gpu::mock 的 tracked_value.h 和 gpu::vk 的 vulkan_tracked_value.h 各自
/// 提供一份，submit 本身不需要知道走的是哪一份。
struct make_pending_t
{
	constexpr
#if __cpp_static_call_operator
	static
#endif
	decltype(auto) operator()(auto&& item, auto&& signal)
#if !__cpp_static_call_operator
	const
#endif
#if defined(__GNUC__) && !defined(__clang__)
		// GCC 在 noexcept-specifier/requires 子句里直接引用外层形参会报错，
		// clang 没有这个限制；隔离出不改变语义的等价写法，见 cpo.h 顶部说明。
		noexcept(noexcept(make_pending_cpo::choice(::std::forward<decltype(item)>(item), ::std::forward<decltype(signal)>(signal)).nothrow ? true : true))
		requires (requires { requires make_pending_cpo::choice(::std::forward<decltype(item)>(item), ::std::forward<decltype(signal)>(signal)).strategy != make_pending_cpo::choose::none; })
#else
		noexcept(make_pending_cpo::choice(::std::forward<decltype(item)>(item), ::std::forward<decltype(signal)>(signal)).nothrow)
		requires (make_pending_cpo::choice(::std::forward<decltype(item)>(item), ::std::forward<decltype(signal)>(signal)).strategy != make_pending_cpo::choose::none)
#endif
	{
		return make_pending(::std::forward<decltype(item)>(item), ::std::forward<decltype(signal)>(signal));
	}
};
inline constexpr make_pending_t make_pending{};

// ---------------------------------------------------------------------------
// transition：sender 世界的定制点。目标状态必须显式写出来——这正是"转到哪里"
// 这个同步决策本身，系统不代替人做这个决定。

/// 查 predecessor 的 completion scheduler，如果它提供 transition_impl 就用它，
/// 否则退化成只调用 transite。两条路径的返回类型必须一致（都是变换后的
/// tracked_resource），所以这里不是简单的 if constexpr 分派到不同返回类型。
/// 第一个参数是 predecessor 的 env（不是整个 sender）：跟 dispatch_consume
/// 的做法一致，只需要能查 get_completion_scheduler，不需要保留整个 predecessor
/// sender 对象。
template <class Env, class Value, class To>
[[nodiscard]] auto dispatch_transition(Env const& pred_env, Value const& value, To to)
{
	if constexpr (requires { ::stdexec::get_completion_scheduler<::stdexec::set_value_t>(pred_env); })
	{
		auto sch = ::stdexec::get_completion_scheduler<::stdexec::set_value_t>(pred_env);
		if constexpr (requires { sch.transition_impl(value, to); })
			return sch.transition_impl(value, to);
		else
			return ::gpu::transite(value, ::std::move(to));
	}
	else
	{
		return ::gpu::transite(value, ::std::move(to));
	}
}

template <class Pred, class To>
struct transition_sender
{
	using sender_concept = ::stdexec::sender_t;

	using __env_t = ::stdexec::env_of_t<Pred>;

	template <class Value>
	using __result_t = decltype(dispatch_transition(::std::declval<__env_t const&>(), ::std::declval<Value const&>(), ::std::declval<To>()));

	// Pred 满足 tracked_resource_sender：完成值唯一，且满足 tracked_resource。
	// 用同一个 detail::single_value_t 提取出这个值类型，套进 __result_t 就是
	// transition_sender 的完成值类型——跟 Pred 的错误/停止信道保持一致，
	// transition 本身不引入新的错误来源（dispatch_transition 是 noexcept 的
	// 纯计算，不会抛）。
	using __value_t = ::stdexec::value_types_of_t<Pred, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>;
	using completion_signatures = ::stdexec::completion_signatures<
		::stdexec::set_value_t(__result_t<__value_t>),
		::stdexec::set_error_t(::std::exception_ptr),
		::stdexec::set_stopped_t()>;

	Pred _pred;
	To _to;

	[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(_pred); }

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		struct inner_receiver
		{
			using receiver_concept = ::stdexec::receiver_t;
			operation* _op;

			template <transitible_tracked_resource Value>
			void set_value(Value&& value) noexcept
			{
				::stdexec::set_value(::std::move(_op->_receiver),
					dispatch_transition(::stdexec::get_env(_op->_pred), value, ::std::move(_op->_to)));
			}

			template <class Error>
			void set_error(Error&& error) noexcept { ::stdexec::set_error(::std::move(_op->_receiver), ::std::forward<Error>(error)); }

			void set_stopped() noexcept { ::stdexec::set_stopped(::std::move(_op->_receiver)); }

			[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(_op->_receiver); }
		};

		// _pred 保留成 operation 自己的成员（生命周期跟 operation 一样长），
		// 用 _pred（左值）去 connect，不是按值收一个函数形参再 move 走它——
		// 已经实测确认过：像 then(pred, fun) 这类标准算子的 get_env() 常常是
		// __sync_attrs{sndr_}，__sndr_ 是指向 sender 表达式节点本身的引用，
		// 不是深拷贝；如果只在一个即将被 move 走的局部形参上取一次 env 存成
		// "快照"，pred 转手之后这个函数形参对象销毁，快照里的引用立刻悬空——
		// 这不是我们自己实现的 bug，是 __sync_attrs 的设计前提（它假设你会一直
		// 通过原来的 sender 对象去查，不会脱离对象生命周期单独保留 env）。
		// dispatch_transition 因此改成直接接收 __env_t（一份不含悬空引用的
		// snapshot 类型形状不变），但调用点从"提前存好的快照"换成"每次都从
		// 活着的 _pred 现查"。
		Pred _pred;
		To _to;
		Receiver _receiver;
		::stdexec::connect_result_t<Pred&, inner_receiver> _inner;

		operation(Pred pred, To to, Receiver receiver)
			: _pred(::std::move(pred)), _to(::std::move(to)), _receiver(::std::move(receiver))
			, _inner(::stdexec::connect(_pred, inner_receiver{this}))
		{
		}

		void start() & noexcept { ::stdexec::start(_inner); }
	};

	template <class Receiver>
	[[nodiscard]] operation<Receiver> connect(Receiver receiver) const &
	{
		return operation<Receiver>{_pred, _to, ::std::move(receiver)};
	}
};

template <tracked_resource_sender Pred, class To>
[[nodiscard]] transition_sender<::std::decay_t<Pred>, To> transition(Pred&& pred, To to)
{
	return {::std::forward<Pred>(pred), ::std::move(to)};
}

template <class To>
struct transition_closure : ::stdexec::sender_adaptor_closure<transition_closure<To>>
{
	To _to;

	template <tracked_resource_sender Pred>
	[[nodiscard]] auto operator()(Pred&& pred) && -> decltype(auto)
	{
		return ::gpu::transition(::std::forward<Pred>(pred), ::std::move(_to));
	}
};

template <class To>
[[nodiscard]] transition_closure<To> transition(To to)
{
	return {{}, ::std::move(to)};
}

// ---------------------------------------------------------------------------
// submit：结束当前批次、真正提交，产出一个 pending_resource。Domain 是显式传入
// 的引用（不查 scheduler）；pending_value 的具体类型交给 make_pending 那个 CPO
// 去决定，submit 自己不认识任何具体类型。

template <tracked_resource_sender Pred, class Domain>
[[nodiscard]] auto submit(Pred&& pred, Domain& dom)
{
	return ::stdexec::let_value(::std::forward<Pred>(pred),
		[&dom]<tracked_resource T>(T const& item)
		{
			auto signal = dom.cut();
			return ::stdexec::just(::gpu::make_pending(item, ::std::move(signal)));
		});
}

template <class Domain>
struct submit_closure : ::stdexec::sender_adaptor_closure<submit_closure<Domain>>
{
	Domain* _domain;

	template <tracked_resource_sender Pred>
	[[nodiscard]] auto operator()(Pred&& pred) && -> decltype(auto)
	{
		return ::gpu::submit(::std::forward<Pred>(pred), *_domain);
	}
};

template <class Domain>
[[nodiscard]] submit_closure<Domain> submit(Domain& dom)
{
	return {{}, &dom};
}

// ---------------------------------------------------------------------------
// consume：pending_resource -> tracked_resource。跟 transition 同一套分派模式——
// 查 predecessor 的 completion scheduler，如果它提供 consume_impl 就用它（域的
// 录制链里，记一条 wait，不做真正的等待），查不到就落回 consume_external 原语
// 本身（它自己决定"没有域"时怎么办，比如 mock 版本会真的等信号）。两条分支都
// 产出 sender，dispatch_consume 用 decltype 统一成同一个返回类型，跟
// dispatch_transition 是同一个手法，不需要 variant/类型擦除去兼容两种形状。
//
// 第一个参数是 predecessor 的 env（不是整个 sender），第二个参数是完成值本身——
// consume_impl 只对 gpu::mock::pending_value 这样的具体类型生效（pending_resource
// 没有像 tracked_resource 那样拆出通用的字段读取原语，按设计讨论先搁置），
// requires 检测不到这条路径就落回默认。
template <class Env, class Value>
[[nodiscard]] auto dispatch_consume(Env const& pred_env, Value const& value)
{
	if constexpr (requires { ::stdexec::get_completion_scheduler<::stdexec::set_value_t>(pred_env); })
	{
		auto sch = ::stdexec::get_completion_scheduler<::stdexec::set_value_t>(pred_env);
		if constexpr (requires { sch.consume_impl(value); })
			return sch.consume_impl(value);
		else
			return ::gpu::consume_external(value);
	}
	else
	{
		return ::gpu::consume_external(value);
	}
}

/// 即便走的是 consume_impl 这条定制路径，仍然不能用标准 let_value 简单包一层：
/// 已实测确认——像 then(pred, fun) 这类标准算子的 get_env() 常常是
/// __sync_attrs{sndr_}，__sndr_ 是指向 sender 表达式节点本身的引用，不是深拷贝。
/// 如果在调用 let_value 之前，把 pred 的 env 取出来存成一份"快照"、通过闭包传给
/// fun，pred 这个局部变量在函数返回后销毁，快照里的引用立刻悬空。正确做法跟
/// transition_sender 一样：自己手写 operation，把 Pred 作为成员保留、用左值
/// connect，每次查询直接对活着的 _pred 现查 env。
template <class Pred>
struct consume_sender
{
	using sender_concept = ::stdexec::sender_t;

	using __env_t = ::stdexec::env_of_t<Pred>;
	using __value_t = ::stdexec::value_types_of_t<Pred, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>;
	using __inner_sender_t = decltype(dispatch_consume(::std::declval<__env_t const&>(), ::std::declval<__value_t const&>()));

	using completion_signatures = ::stdexec::completion_signatures_of_t<__inner_sender_t>;

	Pred _pred;

	[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(_pred); }

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		struct forwarding_receiver
		{
			using receiver_concept = ::stdexec::receiver_t;
			operation* _op;

			template <class... Args>
			void set_value(Args&&... args) noexcept { ::stdexec::set_value(::std::move(_op->_receiver), ::std::forward<Args>(args)...); }
			template <class Error>
			void set_error(Error&& error) noexcept { ::stdexec::set_error(::std::move(_op->_receiver), ::std::forward<Error>(error)); }
			void set_stopped() noexcept { ::stdexec::set_stopped(::std::move(_op->_receiver)); }
			[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(_op->_receiver); }
		};

		struct inner_receiver
		{
			using receiver_concept = ::stdexec::receiver_t;
			operation* _op;

			template <pending_resource Value>
			void set_value(Value&& value) noexcept
			{
				auto& sender_slot = _op->_inner_sender.emplace(detail::emplace_from{[this, &value] { return dispatch_consume(::stdexec::get_env(_op->_pred), value); }});
				auto& op_slot = _op->_inner_op.emplace(detail::emplace_from{[this, &sender_slot] { return ::stdexec::connect(sender_slot, forwarding_receiver{_op}); }});
				::stdexec::start(op_slot);
			}

			template <class Error>
			void set_error(Error&& error) noexcept { ::stdexec::set_error(::std::move(_op->_receiver), ::std::forward<Error>(error)); }

			void set_stopped() noexcept { ::stdexec::set_stopped(::std::move(_op->_receiver)); }

			[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(_op->_receiver); }
		};

		Pred _pred;
		Receiver _receiver;
		::std::optional<__inner_sender_t> _inner_sender{};
		::std::optional<::stdexec::connect_result_t<__inner_sender_t&, forwarding_receiver>> _inner_op{};
		::stdexec::connect_result_t<Pred&, inner_receiver> _pred_op;

		operation(Pred pred, Receiver receiver)
			: _pred(::std::move(pred)), _receiver(::std::move(receiver))
			, _pred_op(::stdexec::connect(_pred, inner_receiver{this}))
		{
		}

		void start() & noexcept { ::stdexec::start(_pred_op); }
	};

	template <class Receiver>
	[[nodiscard]] operation<Receiver> connect(Receiver receiver) const &
	{
		return operation<Receiver>{_pred, ::std::move(receiver)};
	}
};

template <pending_resource_sender Pred>
[[nodiscard]] consume_sender<::std::decay_t<Pred>> consume(Pred&& pred)
{
	return {::std::forward<Pred>(pred)};
}

struct consume_closure : ::stdexec::sender_adaptor_closure<consume_closure>
{
	template <pending_resource_sender Pred>
	[[nodiscard]] auto operator()(Pred&& pred) && -> decltype(auto)
	{
		return ::gpu::consume(::std::forward<Pred>(pred));
	}
};

[[nodiscard]] inline consume_closure consume()
{
	return {};
}
}
