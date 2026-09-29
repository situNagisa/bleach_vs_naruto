#pragma once

#include <utility>

/// 四个原语，按 modern-cpp-concept-design 的 CPO 写法实现：优先走成员函数，
/// 找不到再走 ADL，两条路都没有就在约束检查阶段被排除，没有隐式的默认行为。
///
/// 之所以是 CPO 而不是普通函数：每种资源（image、buffer，以后可能还有别的）自己
/// 决定这四个原语具体怎么实现；tracked_resource/pending_resource 这两个 concept
/// （见 concepts.h）只组合这些原语、约束"调用得通、返回值是什么"，不关心谁来实现，
/// 也不重复这里的查找/分派逻辑。
///
/// choice 用 auto&&/forward 转发真实的调用点实参：检测表达式和 operator() 函数体
/// 里真正执行的表达式必须同源，不用 ::std::declval<T>() 替代——declval 只是"给定
/// 类型构造一个表达式"，不等于"转发这次调用真正传入的实参"，两者在某些成员函数
/// 只对特定值类别可调用的情况下会给出不同的检测结果。
///
/// GCC（截至这份原型验证时用到的版本）在 noexcept-specifier / requires 子句里
/// 直接引用外层函数自身的形参（即便包在 forward<decltype(t)>(t) 里）会报
/// "use of parameter from containing function"；clang 没有这个限制。这是编译器
/// 缺陷，处理方式是隔离出一个不改变语义的等价写法（把调用结果套进
/// noexcept(...) 运算符和 requires(requires{requires ...;}) 复合要求，两者都是
/// 不求值上下文，GCC 不再判定为"直接消费外层形参"），不是换成语义不同的
/// declval——choice() 本身、operator() 函数体内的真实调用，两个分支完全一样。
namespace gpu
{
namespace resource_cpo
{
	// 普通查找屏障：这个声明本身从不会被真的调用，只用来在本命名空间内挡住
	// 外层 `inline constexpr resource_t resource{};` 被无限定名字查找先一步找到——
	// 否则下面 `resource(...)` 这一行会直接递归调用 CPO 对象本身，而不是走 ADL。
	void resource();

	enum class choose { member, adl, none };
	struct choice_result { choose strategy; bool nothrow; };

	consteval choice_result choice(auto&& t) noexcept
	{
		if constexpr (requires { ::std::forward<decltype(t)>(t).resource(); })
			return {choose::member, noexcept(::std::forward<decltype(t)>(t).resource())};
		else if constexpr (requires { resource(::std::forward<decltype(t)>(t)); })
			return {choose::adl, noexcept(resource(::std::forward<decltype(t)>(t)))};
		else
			return {choose::none, true};
	}
}

struct resource_t
{
	constexpr
#if __cpp_static_call_operator
	static
#endif
	decltype(auto) operator()(auto&& t)
#if !__cpp_static_call_operator
	const
#endif
#if defined(__GNUC__) && !defined(__clang__)
		noexcept(noexcept(resource_cpo::choice(::std::forward<decltype(t)>(t)).nothrow ? true : true))
		requires (requires { requires resource_cpo::choice(::std::forward<decltype(t)>(t)).strategy != resource_cpo::choose::none; })
#else
		noexcept(resource_cpo::choice(::std::forward<decltype(t)>(t)).nothrow)
		requires (resource_cpo::choice(::std::forward<decltype(t)>(t)).strategy != resource_cpo::choose::none)
#endif
	{
		constexpr auto strategy = resource_cpo::choice(::std::forward<decltype(t)>(t)).strategy;
		if constexpr (strategy == resource_cpo::choose::member)
			return ::std::forward<decltype(t)>(t).resource();
		else
			return resource(::std::forward<decltype(t)>(t));
	}
};
inline constexpr resource_t resource{};

// state：形状和 resource 完全一样，只是问的是"当前状态"而不是"句柄"。
namespace state_cpo
{
	void state();

	enum class choose { member, adl, none };
	struct choice_result { choose strategy; bool nothrow; };

	consteval choice_result choice(auto&& t) noexcept
	{
		if constexpr (requires { ::std::forward<decltype(t)>(t).state(); })
			return {choose::member, noexcept(::std::forward<decltype(t)>(t).state())};
		else if constexpr (requires { state(::std::forward<decltype(t)>(t)); })
			return {choose::adl, noexcept(state(::std::forward<decltype(t)>(t)))};
		else
			return {choose::none, true};
	}
}

struct state_t
{
	constexpr
#if __cpp_static_call_operator
	static
#endif
	decltype(auto) operator()(auto&& t)
#if !__cpp_static_call_operator
	const
#endif
#if defined(__GNUC__) && !defined(__clang__)
		noexcept(noexcept(state_cpo::choice(::std::forward<decltype(t)>(t)).nothrow ? true : true))
		requires (requires { requires state_cpo::choice(::std::forward<decltype(t)>(t)).strategy != state_cpo::choose::none; })
#else
		noexcept(state_cpo::choice(::std::forward<decltype(t)>(t)).nothrow)
		requires (state_cpo::choice(::std::forward<decltype(t)>(t)).strategy != state_cpo::choose::none)
#endif
	{
		constexpr auto strategy = state_cpo::choice(::std::forward<decltype(t)>(t)).strategy;
		if constexpr (strategy == state_cpo::choose::member)
			return ::std::forward<decltype(t)>(t).state();
		else
			return state(::std::forward<decltype(t)>(t));
	}
};
inline constexpr state_t state{};

// transite(t, to)：两个参数，其余和上面一致。to 是一个值参数——状态既可以是空类型
// （编译期已知的状态，转换等于常量折叠），也可以是带运行时字段的类型，两者对这里
// 的调用形式是同一行代码，零开销与否交给编译器根据实参类型决定。
namespace transite_cpo
{
	void transite();

	enum class choose { member, adl, none };
	struct choice_result { choose strategy; bool nothrow; };

	consteval choice_result choice(auto&& t, auto&& to) noexcept
	{
		if constexpr (requires { ::std::forward<decltype(t)>(t).transite(::std::forward<decltype(to)>(to)); })
			return {choose::member, noexcept(::std::forward<decltype(t)>(t).transite(::std::forward<decltype(to)>(to)))};
		else if constexpr (requires { transite(::std::forward<decltype(t)>(t), ::std::forward<decltype(to)>(to)); })
			return {choose::adl, noexcept(transite(::std::forward<decltype(t)>(t), ::std::forward<decltype(to)>(to)))};
		else
			return {choose::none, true};
	}
}

struct transite_t
{
	constexpr
#if __cpp_static_call_operator
	static
#endif
	decltype(auto) operator()(auto&& t, auto&& to)
#if !__cpp_static_call_operator
	const
#endif
#if defined(__GNUC__) && !defined(__clang__)
		noexcept(noexcept(transite_cpo::choice(::std::forward<decltype(t)>(t), ::std::forward<decltype(to)>(to)).nothrow ? true : true))
		requires (requires { requires transite_cpo::choice(::std::forward<decltype(t)>(t), ::std::forward<decltype(to)>(to)).strategy != transite_cpo::choose::none; })
#else
		noexcept(transite_cpo::choice(::std::forward<decltype(t)>(t), ::std::forward<decltype(to)>(to)).nothrow)
		requires (transite_cpo::choice(::std::forward<decltype(t)>(t), ::std::forward<decltype(to)>(to)).strategy != transite_cpo::choose::none)
#endif
	{
		constexpr auto strategy = transite_cpo::choice(::std::forward<decltype(t)>(t), ::std::forward<decltype(to)>(to)).strategy;
		if constexpr (strategy == transite_cpo::choose::member)
			return ::std::forward<decltype(t)>(t).transite(::std::forward<decltype(to)>(to));
		else
			return transite(::std::forward<decltype(t)>(t), ::std::forward<decltype(to)>(to));
	}
};
inline constexpr transite_t transite{};

// consume_external(t)：形状和 resource/state 一样，只有一个参数——具体是记一条
// "这一批要等的信号"还是真的去等，是实现自己的事（见 tracked_value.h），
// 这个 CPO 只负责把调用转发到位。
namespace consume_external_cpo
{
	void consume_external();

	enum class choose { member, adl, none };
	struct choice_result { choose strategy; bool nothrow; };

	consteval choice_result choice(auto&& t) noexcept
	{
		if constexpr (requires { ::std::forward<decltype(t)>(t).consume_external(); })
			return {choose::member, noexcept(::std::forward<decltype(t)>(t).consume_external())};
		else if constexpr (requires { consume_external(::std::forward<decltype(t)>(t)); })
			return {choose::adl, noexcept(consume_external(::std::forward<decltype(t)>(t)))};
		else
			return {choose::none, true};
	}
}

struct consume_external_t
{
	constexpr
#if __cpp_static_call_operator
	static
#endif
	decltype(auto) operator()(auto&& t)
#if !__cpp_static_call_operator
	const
#endif
#if defined(__GNUC__) && !defined(__clang__)
		noexcept(noexcept(consume_external_cpo::choice(::std::forward<decltype(t)>(t)).nothrow ? true : true))
		requires (requires { requires consume_external_cpo::choice(::std::forward<decltype(t)>(t)).strategy != consume_external_cpo::choose::none; })
#else
		noexcept(consume_external_cpo::choice(::std::forward<decltype(t)>(t)).nothrow)
		requires (consume_external_cpo::choice(::std::forward<decltype(t)>(t)).strategy != consume_external_cpo::choose::none)
#endif
	{
		constexpr auto strategy = consume_external_cpo::choice(::std::forward<decltype(t)>(t)).strategy;
		if constexpr (strategy == consume_external_cpo::choose::member)
			return ::std::forward<decltype(t)>(t).consume_external();
		else
			return consume_external(::std::forward<decltype(t)>(t));
	}
};
inline constexpr consume_external_t consume_external{};
}
