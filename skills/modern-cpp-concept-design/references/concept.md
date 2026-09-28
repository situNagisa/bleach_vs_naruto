# concept编写方法

通过组织 [自定义点对象CPO](CPO(custom point object).md)， [类型特征](类型特征.md)，以及非自定义点组成一个`concept`

## 示例
```cpp
// 原语：函数调用（自定义点），参考[自定义点对象CPO](CPO(custom point object).md)
namespace evaluate_cpo{
enum class choose
{
	member,
	adl,
	prefixed, 
	none, // 没有默认实现
};
struct choice_result
{
	choose strategy;
	bool nothrow;
};

// choice 只按类型参数化，检测表达式用 declval 构造——原因见
// CPO(custom point object).md：直接把运行期实参递给 noexcept()/requires()
// 在目前的编译器上可能报错。
template <class T>
consteval choice_result choice() noexcept // 也允许直接从类型推导出值
{
	// 优先级为：成员函数，ADL，开洞
	if constexpr (requires { ::std::declval<T>().evaluate(); })
	{
		return {choose::member, noexcept(::std::declval<T>().evaluate())};
	}
	else if constexpr (requires { evaluate(::std::declval<T>()); })
	{
		return {choose::adl, noexcept(evaluate(::std::declval<T>()))};
	}
	// 根据具体实现增加分支
	else if constexpr (requires { _vkfu_evaluate(::std::declval<T>()); })
	{
		return {choose::prefixed, noexcept(_vkfu_evaluate(::std::declval<T>()))};
	}
	else
	{
		return {choose::none, true};
	}
}
}
struct evaluate_t
{
	constexpr decltype(auto) operator()(auto&& expression) const
		noexcept(evaluate_cpo::choice<decltype(expression)>().nothrow)
		requires (evaluate_cpo::choice<decltype(expression)>().strategy != evaluate_cpo::choose::none)
	{
		constexpr auto strategy = evaluate_cpo::choice<decltype(expression)>().strategy;
		if constexpr (strategy == evaluate_cpo::choose::member)
		{
			return ::std::forward<decltype(expression)>(expression).evaluate();
		}
		else if constexpr (strategy == evaluate_cpo::choose::adl)
		{
			return evaluate(::std::forward<decltype(expression)>(expression));
		}
		else
		{
			return _vkfu_evaluate(::std::forward<decltype(expression)>(expression));
		}
		// 不可能会出现strategy == choose::none的情况，被约束排除了
	}
};
inline constexpr evaluate_t evaluate{};
```

// 原语：类型特征（自定义点），编写方法参考 [类型特征](类型特征.md)
template<class T>
struct expression_vulkan_tag
{
	using type = typename ::std::remote_reference_t<T>::vulkan_tag_type;
};
template<class T>
	requires requires { typename expression_vulkan_tag<T>::type; }
using expression_vulkan_tag_t = typename expression_vulkan_tag<T>::type;

// 原语不做约束，只做最基本的行为表达，Concept负责组合原语。
template<class T>
concept expression = requires(T t)
{
	{ evaluate(::std::forward<T>(t)) } -> storable; // 给原语添加约束
	requires vulkan_object<expression_vulkan_tag_t<T>>; // 给原语添加约束
};

// Concept衍生算法
// 通过组合Concept和其原语生成类型特征
template<expression T> // 依赖concept
using expression_storage_t = ::std::remove_cvref_t<decltype(evaluate(::std::declval<T>()))>; // 依赖evaluate

template<class T>
concept branch_expression = expression<T> && vulkan_branch_object<expression_vulkan_tag_t<T>>; // 依赖concept, expression_vulkan_tag_t
```
## 原语
`concept`的原语分为两类，一类一类是函数调用，一类是类型特征。
### 函数调用
即普通的函数调用。如果此原语需要允许用户自定义，则可以参考 [自定义点对象CPO](references/CPO(custom point object).md)的编写方法，将此原语编写为自定义点对象。
### 类型特征
即通过这个概念可以引出哪些类型，如此类型特征需要用户自定义，则可以参考 [类型特征](类型特征.md)的自定义点编写方法。
## 组合concept
将原语进行组合约束成概念。

> 注意，原语本身不进行任何约束，不检查参数是否合法，是否满足特征。原语要做的只是进行参数转发，约束在Concept处做。

## 算法
根据Concept和其原语组合出新的操作