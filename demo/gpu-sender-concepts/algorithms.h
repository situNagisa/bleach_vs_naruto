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
/// transition/consume 是"定制点"，但不是靠自己在算子内部手写"查 predecessor 的
/// completion scheduler、if constexpr 分派"——那是我们最早的简化版，调查过
/// nvexec 的 stream_scheduler/stream_domain 之后确认它们不这么做：真正的机制是
/// stdexec::connect() 在真正 connect 之前先调 stdexec::transform_sender(sndr,
/// get_env(rcvr))，这一步会顺着 attrs 转发链查出 predecessor 的 completion
/// domain（stdexec::get_completion_domain_t），如果域提供了针对这个算子 tag 的
/// transform_sender，就把整个 sender 换成域专属的实现——分派是 connect 之前的
/// 一次性结构替换，不是算子内部每次运行时的分支判断。
///
/// transition_sender/consume_sender 因此只是"没有域介入时的默认行为"：跟标准库
/// then_t/let_value_t 的写法一样，是一个普通聚合体——第一个成员是 tag 对象
/// （transition_t/consume_t），第二个是 data，第三个开始是 child sender——
/// stdexec::tag_of_t 靠这个形状（结构化绑定）认出 tag，不需要 stdexec 内部的
/// __sexpr/__make_sexpr 这类不公开实现细节。域（比如 gpu::mock::mock_domain）
/// 在 tracked_value.h 里针对 gpu::transition_t/gpu::consume_t 提供
/// transform_sender，命中就换成调用 scheduler 自己的 transition_impl/
/// consume_impl 的实现；没有域、或者域没接管这个 tag，就走这里的默认聚合体，
/// 默认行为跟以前一致——transition 默认只调用 transite（纯代数变换，不做任何
/// GPU 调用），consume 默认调用 consume_external 原语本身（它自己内部决定
/// "没有域"时怎么办，比如 mock 版本会真的等信号）。
///
/// submit 不一样：它是提交边界，Domain 是显式传入的引用（不是查出来的），
/// 提交之后这条链就离开了域——所以 submit 之后如果还要 consume，走的是
/// "没有域接管"这条默认路径，这是有意的行为，不是遗漏。
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
	// 场景——这里是同一模式在 std::optional 上的等价写法，不依赖任何双下划线的
	// stdexec 内部实体。
	template <class Fn>
	struct call_in_place
	{
		Fn _fn;
		using __t = decltype(_fn());
		operator __t() && { return static_cast<Fn&&>(_fn)(); }
	};
	template <class Fn> call_in_place(Fn) -> call_in_place<Fn>;
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

/// transition 这个算子的 tag：既用来给 stdexec::tag_of_t 识别 transition_sender
/// 这个聚合体，也是域（gpu::mock::mock_domain 等）用来分派 transform_sender 的
/// key——跟标准库 then_t、nvexec then_sender 用同一个类型同时充当两个角色的做法
/// 一致，不需要额外发明一个"算子 id"的概念。
struct transition_t {};

/// 没有域介入时的默认实现：只调用 transite 原语，纯代数变换，不做任何 GPU 调用。
/// 第一个成员是 tag 对象，第二个是 data（目标状态 To），第三个是 child sender——
/// 这三个公开成员的顺序和形状，就是 stdexec::tag_of_t 靠结构化绑定识别 tag 所要求
/// 的全部条件，不需要继承/实现任何 stdexec 内部类型。
template <class To, class Pred>
struct transition_sender
{
	transition_t tag;
	To to;
	Pred pred;

	using sender_concept = ::stdexec::sender_t;

	// Pred 满足 tracked_resource_sender：完成值唯一，且满足 tracked_resource。
	// 默认行为只调用 transite(value, to)，完成值类型是 transite 的返回类型，
	// 不是 Pred 本身的完成值类型——transite 前后状态标签会变（比如 undefined
	// 变成 color_attachment），必须用 decltype(transite(...)) 才能拿到变换后
	// 的真实类型，直接用 Pred 的 __value_t 会跟 set_value 实际传出的类型不一致。
	using __value_t = ::stdexec::value_types_of_t<Pred, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>;
	using __result_t = decltype(::gpu::transite(::std::declval<__value_t const&>(), ::std::declval<To>()));
	using completion_signatures = ::stdexec::completion_signatures<
		::stdexec::set_value_t(__result_t),
		::stdexec::set_error_t(::std::exception_ptr),
		::stdexec::set_stopped_t()>;

	[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(pred); }

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
				::stdexec::set_value(::std::move(_op->_receiver), ::gpu::transite(value, ::std::move(_op->_sndr.to)));
			}

			template <class Error>
			void set_error(Error&& error) noexcept { ::stdexec::set_error(::std::move(_op->_receiver), ::std::forward<Error>(error)); }

			void set_stopped() noexcept { ::stdexec::set_stopped(::std::move(_op->_receiver)); }

			[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(_op->_receiver); }
		};

		// _sndr 保留成 operation 自己的成员（生命周期跟 operation 一样长），
		// 用 _sndr.pred（左值）去 connect，不是按值收一个函数形参再 move 走它——
		// 已经实测确认过：像 then(pred, fun) 这类标准算子的 get_env() 常常是
		// __sync_attrs{sndr_}，__sndr_ 是指向 sender 表达式节点本身的引用，
		// 不是深拷贝；如果只在一个即将被 move 走的局部形参上取一次 env 存成
		// "快照"，pred 转手之后这个函数形参对象销毁，快照里的引用立刻悬空。
		transition_sender _sndr;
		Receiver _receiver;
		::stdexec::connect_result_t<Pred&, inner_receiver> _inner;

		operation(transition_sender sndr, Receiver receiver)
			: _sndr(::std::move(sndr)), _receiver(::std::move(receiver))
			, _inner(::stdexec::connect(_sndr.pred, inner_receiver{this}))
		{
		}

		void start() & noexcept { ::stdexec::start(_inner); }
	};

	template <class Receiver>
	[[nodiscard]] operation<Receiver> connect(Receiver receiver) const &
	{
		return operation<Receiver>{*this, ::std::move(receiver)};
	}
};

template <tracked_resource_sender Pred, class To>
[[nodiscard]] transition_sender<To, ::std::decay_t<Pred>> transition(Pred&& pred, To to)
{
	return {{}, ::std::move(to), ::std::forward<Pred>(pred)};
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
// consume：pending_resource -> tracked_resource。跟 transition 同一套接入方式——
// 默认聚合体只调用 consume_external 原语；域想接管就针对 gpu::consume_t 这个 tag
// 提供 transform_sender，换成调用 scheduler 自己的 consume_impl 的实现。

/// consume 这个算子的 tag，跟 transition_t 是同一种角色。
struct consume_t {};

/// 没有域介入时的默认实现：只调用 consume_external 原语，原语自己决定"没有域"
/// 时怎么办（比如 mock 版本会真的等信号）。没有 data，只有 tag + child 两个
/// 公开成员——stdexec::tag_of_t 一样能靠结构化绑定识别，data 位置放
/// stdexec::__ 这种占位类型没有必要，两个成员的聚合体同样合法。
template <class Pred>
struct consume_sender
{
	consume_t tag;
	Pred pred;

	using sender_concept = ::stdexec::sender_t;

	using __value_t = ::stdexec::value_types_of_t<Pred, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>;
	using __inner_sender_t = decltype(::gpu::consume_external(::std::declval<__value_t const&>()));

	using completion_signatures = ::stdexec::completion_signatures_of_t<__inner_sender_t>;

	[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(pred); }

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
				// std::optional<T>::emplace(args...) 要求 is_constructible_v<T, Args...>——
				// __inner_op 的元素类型是 stdexec::connect_result_t<...>，内部 opstate
				// 不可移动，不能先调用 stdexec::connect(...) 拿到一个值再传给 emplace。
				// __call_in_place 用一个只带隐式转换到 T 的包装类型让 emplace 走隐式
				// 转换构造这条路——跟 stdexec 自己的 __emplace_from（__detail/
				// __utility.hpp，let_value/finally/sequence 都在用）是同一个手法。
				auto& sender_slot = _op->_inner_sender.emplace(::gpu::consume_external(value));
				auto& op_slot = _op->_inner_op.emplace(detail::call_in_place{[this, &sender_slot] { return ::stdexec::connect(sender_slot, forwarding_receiver{_op}); }});
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
		return operation<Receiver>{pred, ::std::move(receiver)};
	}
};

template <pending_resource_sender Pred>
[[nodiscard]] consume_sender<::std::decay_t<Pred>> consume(Pred&& pred)
{
	return {{}, ::std::forward<Pred>(pred)};
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
