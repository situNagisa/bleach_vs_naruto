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

	// choice 用 auto&&/forward 转发真实的调用点实参，检测表达式与 operator()
	// 函数体里实际执行的表达式完全同源——这是唯一正确的语义。不要用
	// ::std::declval<T>() 替代 forward 去构造检测表达式：declval 只是"给定一个
	// 类型，构造一个该类型的表达式"，它不等于"转发这次调用真正传入的实参"；
	// 两者在值类别、cv 限定、甚至可调用性上可能不一致（例如某个成员函数只对
	// 右值可调用，declval<T&>() 和 forward<T>(t)（T 推导为左值引用时）会给出
	// 不同的检测结果）。choice 的检测和 operator() 的实际调用必须是同一行代码
	// 的两次出现，不是语义上"近似"的两行代码。
	consteval choice_result choice(auto&& t) noexcept
	{
		// 顺序由具体实现决定
		if constexpr(requires { ::std::forward<decltype(t)>(t)/*转发参数，保持值类别*/.xxx(::std::forward<decltype(...)>(...)) })
			return { choose::member, noexcept(::std::forward<decltype(t)>(t).xxx(::std::forward<decltype(...)>(...))) };// noexcept子句与requires子句中的表达式相同
		else if constexpr (requires { xxx(::std::forward<decltype(t)>(t)/*转发参数，保持值类别*/)})
			return { choose::adl, noexcept(xxx(::std::forward<decltype(t)>(t))) };
		else ...
		else
			return { choose::none, true };
	}
}
struct xxx_t
{
	constexpr 
#if __cpp_static_call_operator // 用新特性保证性能
	static
#endif
	decltype(auto) operator()(auto&&... args) // 用 auto&&，不要显式声明 template<class T> ... operator()(T&& t)
#if !__cpp_static_call_operator
	const
#endif
	noexcept(xxx_cpo::choice(::std::forward<decltype(args)>(args)...).nothrow)
	requires (xxx_cpo::choice(::std::forward<decltype(args)>(args)...).strategy != xxx_cpo::choose::none) // 若无默认实现，则需要约束排除none
	{
		constexpr choose strategy = xxx_cpo::choice(::std::forward<decltype(args)>(args)...).strategy;
		if constexpr (strategy == choose::member)
			return ::std::forward<decltype(t)>(t)/*转发参数，保持值类别*/.xxx(::std::forward<decltype(...)>(...)); // 注意参数转发以及表达式需要跟以上的表达式相同。
		else if constexpr(strategy == adl)
			return xxx(::std::forward<decltype(t)>(t)/*转发参数，保持值类别*/); // 直接 ADL 调用，不要包一层转发函数
		else
			// 如果有默认实现，在这里实现。
			// 没有默认实现，这里则需要静态断言失败。
			// 不可直接static_assert(false)，这种语法会导致编译一定失败，需要引入模板参数
	}
};
inline constexpr xxx_t xxx{};
```

##  补全骨架时：

- `choice` 是 `consteval` 函数，用 `auto&&` 接收实参、用 `::std::forward<decltype(t)>(t)` 转发；它内部的检测表达式和 `operator()` 函数体里真正执行的表达式必须是同一行代码的两次出现（一次在 `requires{}`/`noexcept(...)` 里判断，一次真正调用），不允许用 `::std::declval<T>()` 替代 `forward` 去"模拟"这次调用——declval 只能出现在真正不需要求值的位置，一旦某个位置的表达式会被实际求值（包括作为 `choice` 的返回值参与 `noexcept(...)` 之外的计算），就必须用 `forward`。
- `operator()` 用 `auto&&...` 接收参数，不要显式写 `template <class T> ... operator()(T&& t)` 再把 `t` 传给 `noexcept()`/`requires()`。
- `operator()` 共享选择结果：以 `requires` 排除 `none`，以条件 `noexcept` 使用 `nothrow`，函数体按 `strategy` 用 `if constexpr` 分派。
- 检测和执行保持相同的参数值类别与返回语义；选择过程不执行用户操作。

按需加入分支，顺序由具体操作决定：

- **成员调用**：检测并执行 `::std::forward<T>(t).xxx(...)`。
- **ADL**：在实现命名空间声明 `void xxx();` 作为普通查找屏障，在其中以未限定的 `xxx(...)` 检测和调用真实实参；不要额外包一层"转发到 ADL"的辅助函数——那层包装不提供任何检测或分派职责，只会重复一次已经在 `choice`/`operator()` 里做过的转发。
- **默认实现**：检查默认实现所需条件，选择对应策略并调用内部实现。
- **其他行为**：增加枚举项及其检测、异常规格与执行分支。

这些分支均可选；固定的是代码组织及选择结果的共享方式。
