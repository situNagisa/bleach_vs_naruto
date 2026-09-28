#pragma once

#include <concepts>

#include <stdexec/execution.hpp>

#include "./cpo.h"

/// tracked_resource / pending_resource：只组合 cpo.h 里的原语，不重复它们的分派逻辑，
/// 也不引入任何 Vulkan 专属的形状（stage/access/layout 属于 image_state.h 这一层，
/// 是叠在这两个 concept 之上的、更具体的约束，见该文件顶部的说明）。
namespace gpu
{
namespace detail
{
	// stdexec::value_types_of_t 要求 Tuple/Variant 是 "template<class...> class"；
	// 这里只关心"唯一一种完成方式、唯一一个值"这个形状——参数个数不为一时这个模板
	// 没有 ::type，让上层的 requires 表达式自然判否，而不是触发主模板的硬错误。
	template <class... Ts>
	struct single_value;

	template <class T>
	struct single_value<T> { using type = T; };

	template <class... Ts>
	using single_value_t = typename single_value<Ts...>::type;

	// 编译期分支落到"不该发生"的那一支时用来触发一条有意义的诊断，而不是裸 static_assert(false)。
	template <class...>
	inline constexpr bool always_false = false;
}

/// 只做值语义 + 调用形状检查，刻意不递归。判断"transite 产出的 sender，它的值
/// 是不是也满足 tracked_resource"这件事，放在下面 tracked_resource_sender 这个
/// 派生 concept 里做，不写进 tracked_resource 自己的定义——在一个只有一个类型
/// 参数的 concept 内部要求"对某个不确定的 To 都成立"没法一般地表达，勉强写成
/// 递归引用自己也只是把问题往后挪；约束应该逐级施加：这里只验证"调用得通、是个
/// sender"，链上的下一个节点会用具体拿到的类型重新独立检查一遍。
///
/// resource()/state() 直接用 `{ expr } -> std::copyable` 约束：这要求表达式本身
/// 的类型满足 copyable，也就是要求 resource/state 按值返回（copyable 蕴含
/// is_object，引用类型永远不满足）。这不是约束表达能力不够、需要绕开引用类别
/// 去看"退引用之后的类型"——按值返回本来就是这两个原语该有的契约：tracked_value
/// 的状态多数是空类型或几个整数字段，按值返回本身几乎零成本，也让"这个值可以被
/// 安全地复制、存进 pending_value"这件事在类型层面直接成立，不需要调用方另外
/// 判断"这是不是一个悬空引用"。
template <class T>
concept tracked_resource = requires(T const& t)
{
	{ ::gpu::resource(t) } -> ::std::copyable;
	{ ::gpu::state(t) } -> ::std::copyable;
} && requires(T const& t, decltype(::gpu::state(t)) to)
{
	{ ::gpu::transite(t, to) } -> ::stdexec::sender;
};

template <class T>
concept pending_resource = requires(T const& t)
{
	{ ::gpu::consume_external(t) } -> ::stdexec::sender;
};

/// 派生：一个 sender，唯一的完成值满足 tracked_resource / pending_resource。
/// 值类型的提取用 stdexec 公开的 value_types_of_t，不重新实现完成签名的收集逻辑；
/// 这两个 concept 之所以能不递归地引用 tracked_resource/pending_resource，是因为
/// 它们是"派生"的一层——由 tracked_resource 组合出来，不是 tracked_resource 自己
/// 引用自己。
template <class S>
concept tracked_resource_sender = ::stdexec::sender_in<S>
	&& requires { typename ::stdexec::value_types_of_t<S, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>; }
	&& tracked_resource<::stdexec::value_types_of_t<S, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>>;

template <class S>
concept pending_resource_sender = ::stdexec::sender_in<S>
	&& requires { typename ::stdexec::value_types_of_t<S, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>; }
	&& pending_resource<::stdexec::value_types_of_t<S, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>>;
}
