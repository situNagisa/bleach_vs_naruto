# CPO编写方法

## 代码组织
遵循以下骨架；私有命名空间使用库内具名的实现细节命名空间。

```cpp
namespace xxx_cpo
{
	enum class choose 
	{ 
		none, // 若无默认实现
		// 按需增加策略
		member,
		adl,
		...
	};
	struct choice_result 
	{ 
		choose strategy; 
		bool nothrow; 
	};

	// choice 只接受类型参数，不接受运行期实参：operator() 会把 choice<...>() 的
	// 结果直接嵌进 noexcept-specifier 和 requires 子句，若 choice 改成接受转发引用
	// 参数（如 `choice(auto&& t)`），一旦调用点是非模板函数（或 t 恰好是外层函数
	// 自身的形参），在目前的编译器上会报 "use of parameter from containing
	// function"（GCC 已确认；这不是本规范定义的行为，是相关编译器的限制，行为
	// 可能随版本变化，但目前必须按此约束编写）。
	template <class... T>
	consteval choice_result choice() noexcept
	{
		// 用 ::std::declval<T>() 在 requires/noexcept 这类不求值上下文中构造检测
		// 表达式——这正是 declval 的设计用途。它不出现在会被求值的返回语句里，
		// 也不替代 operator() 里的真实调用。
		if constexpr (requires { ::std::declval<T...>().xxx(::std::declval<...>()) })
			return { choose::member, noexcept(::std::declval<T...>().xxx(::std::declval<...>())) }; // noexcept子句与requires子句中的表达式相同
		else if constexpr (requires { xxx(::std::declval<T...>()) }) // 见下方 ADL 分支的屏障写法
			return { choose::adl, noexcept(xxx(::std::declval<T...>())) };
		else // ...
			return { choose::none, true };
	}
}
struct xxx_t
{
	// operator() 用 auto&&，不显式声明 template<class T>：转发引用类型交给
	// decltype(t) 取回，同样是为了避免把"外层函数的参数"直接递给 noexcept()。
	constexpr
#if __cpp_static_call_operator // 用新特性保证性能
	static
#endif
	decltype(auto) operator()(auto&&... args)
#if !__cpp_static_call_operator
	const
#endif
		noexcept(xxx_cpo::choice<decltype(args)...>().nothrow)
		requires (xxx_cpo::choice<decltype(args)...>().strategy != xxx_cpo::choose::none) // 若无默认实现，则需要约束排除none
	{
		constexpr auto strategy = xxx_cpo::choice<decltype(args)...>().strategy;
		if constexpr (strategy == xxx_cpo::choose::member)
			return ::std::forward<decltype(args)>(args)...(实际按具体签名转发).xxx(::std::forward<...>(...)); // 这里才是真实调用：用 forward 转发实参本身，不用 declval
		else if constexpr (strategy == xxx_cpo::choose::adl)
			return xxx(::std::forward<decltype(args)>(args)...); // 直接 ADL 调用，不要包一层转发函数
		else
			// 如果有默认实现，在这里实现。
			// 没有默认实现，这里则需要静态断言失败。
			// 不可直接static_assert(false)，这种语法会导致编译一定失败，需要引入模板参数
	}
};
inline constexpr xxx_t xxx{};
```

##  补全骨架时：

- `choice` 是只按类型参数化的 `consteval` 函数模板（`choice<T...>()`），不接受运行期形参。检测表达式内部用 `::std::declval<T>()` 构造，返回的 `{策略, noexcept(实际表达式)}` 里的 `noexcept(...)` 同样基于 `declval` 构造的表达式，不是基于某次具体调用的实参。
- `operator()` 用 `auto&&` 转发实参，通过 `decltype(args)...` 取回类型传给 `choice`；不要显式写 `template <class T> ... operator()(T&& t)` 再把 `t`（而不是 `decltype(t)`／模板形参 `T`）递给 `noexcept()`/`requires()`——那样等于把外层函数的运行期参数直接喂给需要在约束检查阶段求值的 `consteval` 调用，属于上面骨架注释里说明的编译器限制场景。
- `operator()` 的函数体内部才做真实调用：用 `::std::forward<decltype(args)>(args)` 转发实参本身，语义上等价于调用点的真实值类别；不要在函数体里用 `declval` 替代 `forward`——`declval` 产出的不是"一个值"，只在不求值上下文合法，用它替代真实调用会在需要实际求值的位置编译失败，或者（若恰好只用在另一处不求值上下文里）默默割裂"检测用的表达式"与"实际执行的表达式"，使二者不再保证一致。
- 检测和执行必须是同一条表达式（只是 `choice` 里的 `declval` 换成 `operator()` 里的 `forward`），保证选择器判断的可用性、`noexcept` 规格与实际执行路径三者一致；选择过程不执行用户操作。

按需加入分支，顺序由具体操作决定：

- **成员调用**：检测用 `::std::declval<T>().xxx(...)`；执行用 `::std::forward<T>(t).xxx(...)`。
- **ADL**：在实现命名空间声明 `void xxx();` 作为普通查找屏障，在 `choice` 内部用 `::std::declval<T>()` 构造的表达式做未限定检测；`operator()` 里对应分支直接写未限定的 `xxx(::std::forward<T>(t), ...)` 完成调用——不要额外包一层"转发到 ADL"的辅助函数：那层包装不提供任何检测或分派职责，只会重复一次已经在 `choice`/`operator()` 里做过的转发。
- **默认实现**：检查默认实现所需条件，选择对应策略并调用内部实现。
- **其他行为**：增加枚举项及其检测、异常规格与执行分支。

这些分支均可选；固定的是代码组织及选择结果的共享方式。
