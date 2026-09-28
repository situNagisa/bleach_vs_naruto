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
/// choice 只按类型参数化（`choice<T>()`），不接受运行期实参：把运行期形参直接递给
/// noexcept()/requires() 里的 consteval 调用，在这份 profile 用的编译器版本上
/// GCC 会报 "use of parameter from containing function"（clang 没有这个限制）。
/// choice 内部用 ::std::declval<T>() 在 requires/noexcept 这类不求值上下文构造
/// 检测表达式——这是 declval 的设计用途；真正的调用发生在 operator() 里，用
/// ::std::forward 转发实参本身，跟检测表达式一一对应，两者语义保持一致。
/// operator() 本身用 auto&&，不显式写模板参数列表，类型通过 decltype(t) 取回。
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

	template <class T>
	consteval choice_result choice() noexcept
	{
		if constexpr (requires { ::std::declval<T>().resource(); })
			return {choose::member, noexcept(::std::declval<T>().resource())};
		else if constexpr (requires { resource(::std::declval<T>()); })
			return {choose::adl, noexcept(resource(::std::declval<T>()))};
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
		noexcept(resource_cpo::choice<decltype(t)>().nothrow)
		requires (resource_cpo::choice<decltype(t)>().strategy != resource_cpo::choose::none)
	{
		constexpr auto strategy = resource_cpo::choice<decltype(t)>().strategy;
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

	template <class T>
	consteval choice_result choice() noexcept
	{
		if constexpr (requires { ::std::declval<T>().state(); })
			return {choose::member, noexcept(::std::declval<T>().state())};
		else if constexpr (requires { state(::std::declval<T>()); })
			return {choose::adl, noexcept(state(::std::declval<T>()))};
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
		noexcept(state_cpo::choice<decltype(t)>().nothrow)
		requires (state_cpo::choice<decltype(t)>().strategy != state_cpo::choose::none)
	{
		constexpr auto strategy = state_cpo::choice<decltype(t)>().strategy;
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

	template <class T, class To>
	consteval choice_result choice() noexcept
	{
		if constexpr (requires { ::std::declval<T>().transite(::std::declval<To>()); })
			return {choose::member, noexcept(::std::declval<T>().transite(::std::declval<To>()))};
		else if constexpr (requires { transite(::std::declval<T>(), ::std::declval<To>()); })
			return {choose::adl, noexcept(transite(::std::declval<T>(), ::std::declval<To>()))};
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
		noexcept(transite_cpo::choice<decltype(t), decltype(to)>().nothrow)
		requires (transite_cpo::choice<decltype(t), decltype(to)>().strategy != transite_cpo::choose::none)
	{
		constexpr auto strategy = transite_cpo::choice<decltype(t), decltype(to)>().strategy;
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

	template <class T>
	consteval choice_result choice() noexcept
	{
		if constexpr (requires { ::std::declval<T>().consume_external(); })
			return {choose::member, noexcept(::std::declval<T>().consume_external())};
		else if constexpr (requires { consume_external(::std::declval<T>()); })
			return {choose::adl, noexcept(consume_external(::std::declval<T>()))};
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
		noexcept(consume_external_cpo::choice<decltype(t)>().nothrow)
		requires (consume_external_cpo::choice<decltype(t)>().strategy != consume_external_cpo::choose::none)
	{
		constexpr auto strategy = consume_external_cpo::choice<decltype(t)>().strategy;
		if constexpr (strategy == consume_external_cpo::choose::member)
			return ::std::forward<decltype(t)>(t).consume_external();
		else
			return consume_external(::std::forward<decltype(t)>(t));
	}
};
inline constexpr consume_external_t consume_external{};
}
