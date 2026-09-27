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
	
	consteval choice_result choice(args...) noexcept
	{
		// 顺序由具体实现决定
		if constexpr(requires { forward(t)/*转发参数，保持值类别*/.xxx(forward(...)) })
			return { member, noexcept(forward(t).xxx(forward(...))) };// noexcept子句与requires子句中的表达式相同
		else if constexpr (requires { xxx(forward(...)/*转发参数，保持值类别*/)})
			return { adl, noexcept(xxx(forward(...))) };
		else ...
		else
			return { none, true };
	}
}
struct xxx_t
{
    constexpr 
#if __cpp_static_call_operator // 用新特性保证性能
	static
#endif
    decltype(auto) operator()(args...) // 参数根据具体实现决定
#if !__cpp_static_call_operator
	const
#endif
	noexcept(xxx_cpo::choice(forward(args...)/*转发参数，保持值类别*/).nothrow)
	requires (xxx_cpo::choice(forward(args...)).strategy != none) // 若无默认实现，则需要约束排除none
    {
	    constexpr choose strategy = xxx_cpo::choice(forward(args...)/*转发参数，保持值类别*/).strategy;
	    if constexpr (strategy == member)
		    return forward(t)/*转发参数，保持值类别*/.xxx(forward(...)); // 注意参数转发以及表达式需要跟以上的表达式相同。
		else if constexpr(strategy == adl)
			return xxx(forward(...)/*转发参数，保持值类别*/)
		else
			// 如果有默认实现，在这里实现。
			// 没有默认实现，这里则需要静态断言失败。
			// 不可直接static_assert(false)，这种语法会导致编译一定失败，需要引入模板参数
    }
};
inline constexpr xxx_t xxx{};
```

##  补全骨架时：

- `choice` 用 `requires` 和 `if constexpr` 选择策略，在有效分支内返回 `{策略, noexcept(实际表达式)}`；无可用实现则返回 `none`。
- `operator()` 共享选择结果：以 `requires` 排除 `none`，以条件 `noexcept` 使用 `nothrow`，函数体按 `strategy` 用 `if constexpr` 分派。
- 检测和执行保持相同的参数值类别与返回语义；选择过程不执行用户操作。选择器也可写成无参的 `choice<T, ...>()`。

按需加入分支，顺序由具体操作决定：

- **成员调用**：检测并执行 `::std::forward<T>(t).xxx(...)`。
- **ADL**：在实现命名空间声明 `void xxx();` 作为普通查找屏障，在其中以未限定的 `xxx(...)` 检测和调用；外部调用对象通过该命名空间的辅助函数执行此分支。
- **默认实现**：检查默认实现所需条件，选择对应策略并调用内部实现。
- **其他行为**：增加枚举项及其检测、异常规格与执行分支。

这些分支均可选；固定的是代码组织及选择结果的共享方式。