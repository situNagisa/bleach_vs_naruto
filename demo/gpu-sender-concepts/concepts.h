#pragma once

#include <concepts>

#include <stdexec/execution.hpp>

#include "./cpo.h"

/// 两组代数体系，刻意分开：
///
/// 1. tracked_resource / transitible_tracked_resource：纯粹的资源变换代数，
///    只用 resource/state/transite 三个原语，完全不涉及 sender。transite(t, to)
///    的契约是"给一个 T，给一个目标状态，产出另一个 T"（就是它自己，不是一个
///    sender、不是别的类型）——这个代数在自己的世界里闭环：从 tracked_resource
///    出发，通过 transite 变换，结果还是 tracked_resource，不会逃出到别的概念
///    里去。这跟"resource/state 这两个原语描述一个资源的静态形状，transite
///    描述这个资源在同一套形状内的状态转换"是同一件事的两种说法。
///
/// 2. tracked_resource_sender：sender 世界的概念，跟上面那组代数没有继承关系，
///    只是"完成值满足 tracked_resource"这一层检查。algorithms.h 里的
///    transition() 是作用在这一层的独立算法——它接一个 tracked_resource_sender，
///    产出另一个 tracked_resource_sender，内部调用 transite 这个代数原语去做
///    真正的变换。domain 怎么随 sender 链传递，是 transition 这个算法自己的
///    问题，跟 transite/tracked_resource 这两个纯代数概念完全无关：transite
///    本身不知道 sender、不知道 domain，它只是"一个值到另一个值"的变换。
///
/// 两者的关系是：transition（sender 算法）= 在 sender 完成时调用 transite
/// （代数原语），仅此而已。pending_resource 那一侧（consume_external/consume）
/// 目前先不做同样的拆分，保持现状，按需再处理。
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

/// 只做值语义检查：resource()/state() 都按值返回。这是整套代数体系最基础的一层，
/// 不涉及 transite、不涉及 sender——一个只有静态形状、没有状态转换能力的资源
/// 也满足这个 concept（比如一个只读的常量资源）。
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
};

/// tracked_resource 的细化：额外要求 transite(t, to) 产出的还是同一个 T（不是
/// sender，不是别的类型）——闭环发生在这里。用嵌套 requires 直接要求返回类型
/// same_as<T>，不是"满足 tracked_resource"这种较弱的约束：transite 变换前后
/// 资源的具体类型必须不变（状态可以变，比如 tracked_value<Handle, From> 变成
/// tracked_value<Handle, To>，那是同一个类型模板的不同实例化，各自都单独满足
/// transitible_tracked_resource，不需要在这条约束里体现"变换前后是同一个模板
/// 不同实例化"这种更精细的关系——那是 transite 原语自己的实现契约，不是概念
/// 层要表达的东西）。
///
/// 这里同样不递归检查"transite 的返回类型是否也满足 transitible_tracked_resource"
/// ——原因跟之前版本一致：在一个只有一个类型参数的 concept 内部要求"对某个不
/// 确定的 To 都成立"没法一般地表达；这个约束只验证"给定 T 的 state() 类型作为
/// to，transite 产出的确实还是 T"，链上下一次变换会用具体拿到的 To 重新构造
/// 一次独立的检查。
template <class T>
concept transitible_tracked_resource = tracked_resource<T> && requires(T const& t, decltype(::gpu::state(t)) to)
{
	{ ::gpu::transite(t, to) } -> ::std::same_as<T>;
};

template <class T>
concept pending_resource = requires(T const& t)
{
	{ ::gpu::consume_external(t) } -> ::stdexec::sender;
};

/// 派生：一个 sender，唯一的完成值满足 tracked_resource（transition 这个 sender
/// 算法要求的是这一层，不要求 transitible_tracked_resource——transition 自己
/// 才是"给 sender 世界补上变换能力"的地方，它内部会再对具体拿到的值类型检查
/// transitible_tracked_resource，这里只检查"这是个完成值为 tracked_resource
/// 的 sender"这个更基础的事实）。值类型的提取用 stdexec 公开的
/// value_types_of_t，不重新实现完成签名的收集逻辑。
template <class S>
concept tracked_resource_sender = ::stdexec::sender_in<S>
	&& requires { typename ::stdexec::value_types_of_t<S, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>; }
	&& tracked_resource<::stdexec::value_types_of_t<S, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>>;

template <class S>
concept pending_resource_sender = ::stdexec::sender_in<S>
	&& requires { typename ::stdexec::value_types_of_t<S, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>; }
	&& pending_resource<::stdexec::value_types_of_t<S, ::stdexec::env<>, detail::single_value_t, detail::single_value_t>>;
}
