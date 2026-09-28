#pragma once

#include <utility>

#include <stdexec/execution.hpp>

#include "./concepts.h"

/// 三个派生算法：transition / submit / consume。每个都提供两种形式——直接调用
/// `algorithm(pred, ...)`，和闭包 `algorithm(...)` 供 `pred | algorithm(...)` 管道
/// 起来。它们只是把 cpo.h 里的原语接进标准的 let_value 链，不重新实现任何分派逻辑。
///
/// 这三个算法都不认识任何具体的 domain/pending_value 类型（不 `#include
/// "./tracked_value.h"` 或 "./vulkan_tracked_value.h"）——它们只用 concepts.h 里
/// 的 tracked_resource/pending_resource 这两个 concept，以及 cpo.h 里的
/// transite/consume_external 两个 CPO。submit 是三者里唯一需要"提交这一批"的，
/// 但它连"批次"该长什么样都不关心：只要求 Domain 提供一个 `cut()` 成员，返回值
/// 满足 make_pending(item, signal) 这个 CPO（见下）能消费的形状。这样
/// gpu（mock）和 gpu::vk（真实 Vulkan）两套完全不同的 domain/pending_value 实现，
/// 都能复用同一份 submit，不需要 submit 知道 pending_value 具体是哪个类型模板。
namespace gpu
{
namespace make_pending_cpo
{
	void make_pending();

	enum class choose { adl, none };
	struct choice_result { choose strategy; bool nothrow; };

	template <class T, class Signal>
	consteval choice_result choice() noexcept
	{
		if constexpr (requires { make_pending(::std::declval<T>(), ::std::declval<Signal>()); })
			return {choose::adl, noexcept(make_pending(::std::declval<T>(), ::std::declval<Signal>()))};
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
		noexcept(make_pending_cpo::choice<decltype(item), decltype(signal)>().nothrow)
		requires (make_pending_cpo::choice<decltype(item), decltype(signal)>().strategy != make_pending_cpo::choose::none)
	{
		return make_pending(::std::forward<decltype(item)>(item), ::std::forward<decltype(signal)>(signal));
	}
};
inline constexpr make_pending_t make_pending{};

// ---------------------------------------------------------------------------
// transition：把 tracked_resource 转到某个目标状态。目标状态必须显式写出来——
// 这正是"转到哪里"这个同步决策本身，系统不代替人做这个决定；Handle/From 全部
// 从上游的值类型推导。

template <tracked_resource_sender Pred, class To>
[[nodiscard]] auto transition(Pred&& pred, To to)
{
	return ::stdexec::let_value(::std::forward<Pred>(pred),
		[to = ::std::move(to)]<tracked_resource T>(T const& item)
		{
			return ::gpu::transite(item, to);
		});
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
// submit：结束当前批次、真正提交，产出一个 pending_resource。Domain 是模板参数，
// 只要求提供 cut() 成员；pending_value 的具体类型交给 make_pending 那个 CPO 去决定，
// submit 自己不认识任何具体类型。

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
// consume：pending_resource -> tracked_resource，纯粹包一层 consume_external。

template <pending_resource_sender Pred>
[[nodiscard]] auto consume(Pred&& pred)
{
	return ::stdexec::let_value(::std::forward<Pred>(pred),
		[]<pending_resource T>(T const& item) { return ::gpu::consume_external(item); });
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

