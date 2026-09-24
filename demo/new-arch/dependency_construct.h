#pragma once

#include <array>
#include <bitset>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <type_traits>

#include "manual_lifetime.h"


/// Target 在 T... 里的下标，编译期常量，没有任何运行时查找。
/// Target 不在 T... 里 => 编译错误（下面走到 throw，consteval 调用点
/// 要求整个求值是常量表达式，求值中走到 throw 就不满足，这次调用本身
/// 编译不过——不是 SFINAE 可探测的替换失败）。
///
/// 用一次 fold 表达式把"每个 T 是不是 Target"摊平成一个 bool 数组，
/// 再在这个普通数组上线性找第一个 true——只实例化这一份函数模板，不像
/// 逐层剥 First/Rest 的递归特化那样要为 N 个类型另外实例化 N 层特化。
///
/// C++26 的 pack indexing（`T...[i]`）本来看着更直接，试过了但用不了：
/// 它要求下标是真正的常量表达式（模板参数、字面量），哪怕整个函数是
/// consteval、下标变量的值在当前这次求值里确实已知，一个普通的循环变量
/// 仍然不满足这个语法层面的要求（clang/g++ 均拒绝，见 tmp/rev 的探测记录）。
/// 数组下标没有这个限制，所以退而求其次：pack 展开只用来摊平成数组，
/// 数组本身用普通循环变量去查。
///
/// 有反射（P2996）能用时换一种摊平方式：`^^T == ^^Target` 直接比较类型的
/// 反射值，语义上跟 `is_same_v` 等价，换上它不是为了更快——两者都只实例化
/// 这一份函数模板——而是反射版不需要 `<type_traits>` 那套 trait 机器，编译期
/// 开销更低。宏保护用 `__cpp_impl_reflection`，不是标准最终定下的
/// `__cpp_reflection`：目前 g++ 16 的实验实现只定义前者，且必须显式加
/// `-freflection`；不加这个 flag 时宏不成立，预处理阶段整段 `#if` 连同里面
/// 的 `^^` 语法一起被跳过，两个编译器默认构建都走下面 is_same_v 那条路，
/// 不受影响。clang 22 目前完全没有反射支持（`^^` 会被当成 blocks 语法解析，
/// 不是宏缺失的问题，探测记录同样在 tmp/rev）。
template <class Target, class... T>
consteval ::std::size_t pack_index_impl()
{
#if defined(__cpp_impl_reflection)
	constexpr bool matches[] = { (^^T == ^^Target)... };
#else
	constexpr bool matches[] = { ::std::is_same_v<Target, T>... };
#endif
	for (::std::size_t index = 0; index < sizeof...(T); ++index)
	{
		if (matches[index])
		{
			return index;
		}
	}
	throw "dependency_construct: Target 不在 T... 里";
}

template <class Target, class... T>
inline constexpr ::std::size_t pack_index_v = pack_index_impl<Target, T...>();


/// 单个类型这一半的约束，拆成独立的 concept 单独命名（而不是直接把
/// `requires` 表达式塞进下面的折叠表达式）——两者语义等价，但把 `requires`
/// 表达式直接摆进折叠表达式会踩到 GCC 处理这类构造时的一个已知内部错误。
template <class D, class Target>
concept resolves_to = requires (D& self) {
	{ self.template resolve<Target>() } -> ::std::same_as<Target&>;
};

/// D 是 T... 这一组类型的构造顺序原语：每个类型正好一份，构造顺序由谁在自己的
/// 构造函数里递归 resolve 了谁来决定（路线 B），不预先声明依赖图。
///
/// 这条 concept 只规定行为：对每个 T，`self.resolve<T>()` 必须能编译、必须
/// 返回 `T&`。存储怎么做、state 放哪、要不要一次性种几个根节点，都是实现
/// 细节，concept 不管。
template <class D, class... T>
concept dependency_construct = (resolves_to<D, T> && ...);


/// 静态情况的一份实现：T... 编译期定死，每个类型正好一份存储。
///
/// 每个 T 继承一份 `manual_lifetime<T>` 当自己的槽位——resolve 时用
/// `static_cast`/显式限定名找到对应的槽，没有类型擦除、没有运行时查找。
/// state（造没造、正不正在造）集中放两个 `bitset`，不挂在每个槽位里：
/// 挂在槽位里的话，每个槽都要为一个 bool 摊上一整个对齐单位的 padding，
/// N 个类型就是 N 份浪费；集中放两个 `sizeof...(T)` 位的 bitset 通常整体
/// 常驻缓存，不产生这份浪费。
///
/// T 的构造函数形如 `T(static_dependency_construct& self)`，函数体里可以
/// 递归调 `self.resolve<U>()` 把依赖拽出来。递归打回自己正在造的类型就是
/// 环，当场 `assert`。
///
/// 销毁按真实构造顺序的逆序（不是 T... 的声明顺序）：后声明的完全可能
/// 因为被先声明的依赖而先造出来。真实构造顺序单独记一份定长数组，销毁
/// 时逆着走；因为最多记 `sizeof...(T)` 条、数量编译期已知，不用堆分配。
template <class... T>
struct static_dependency_construct : manual_lifetime<T>...
{
	static_dependency_construct() = default;

	static_dependency_construct(static_dependency_construct const&) = delete;
	static_dependency_construct& operator=(static_dependency_construct const&) = delete;
	static_dependency_construct(static_dependency_construct&&) = delete;
	static_dependency_construct& operator=(static_dependency_construct&&) = delete;

	~static_dependency_construct()
	{
		for (auto index = _build_count; index != 0; )
		{
			--index;
			_destroy_at(_build_order[index]);
		}
	}

	/// @pre 没有在 Target 自己的构造函数里再次 resolve 自己（会被 assert 抓住）。
	template <class Target>
	[[nodiscard]] Target& resolve()
	{
		constexpr auto index = pack_index_v<Target, T...>;

		if (_built[index])
		{
			return this->template manual_lifetime<Target>::get();
		}

		assert(!_building[index] && "dependency_construct: 构造顺序成环");
		_building[index] = true;

		try
		{
			this->template manual_lifetime<Target>::construct_from(
				[this]() -> Target { return Target(*this); });
		}
		catch (...)
		{
			_building[index] = false;
			throw;
		}

		_building[index] = false;
		_built[index] = true;
		// 下面两步不可能抛：定长数组、内建类型，不涉及分配。
		_build_order[_build_count] = index;
		++_build_count;
		return this->template manual_lifetime<Target>::get();
	}

	void _destroy_at(::std::size_t index) noexcept
	{
		// 按下标把销毁请求分派到对应的 manual_lifetime<T>::destroy()。
		// table 的第 i 项对应 T...里第 i 个类型——跟 fold 展开的顺序、跟
		// pack_index_v 给出的下标，三者天然一致，不用另外核对。
		static constexpr auto table = ::std::array<void (*)(static_dependency_construct&) noexcept, sizeof...(T)>{
			(+[](static_dependency_construct& self) noexcept { self.template manual_lifetime<T>::destroy(); })...
		};
		table[index](*this);
	}

	::std::bitset<sizeof...(T)> _built;
	::std::bitset<sizeof...(T)> _building;
	::std::array<::std::size_t, sizeof...(T)> _build_order{};
	::std::size_t _build_count = 0;
};


/// 包一层引用，把已经存在的对象 D 转发过去，同时显式声明"我转发 T... 这些
/// 类型"——自己不新造任何东西、不管生命周期，resolve<Target>() 原样转给
/// _inner。
///
/// T... 必须显式给，不能从"D 满足 dependency_construct 概念"反推：那条
/// concept 只保证 `self.resolve<Target>()` 这个调用点声明上合法，不保证
/// "Target 其实不在 D 支持的范围内时，编译期能探测出来"——pack_index 找不到
/// Target 是刻意设计成硬错误（consteval 里走到 throw），不是替换失败，
/// `requires resolves_to<D, Target>` 这种约束因此对任何 Target 都成立（真探
/// 测过：见 tmp/rev 的 probe_resolves_to.cpp），没法拿来当筛选依据。显式列出
/// T... 把"这个转发器认领哪些类型"从"问 D 内部怎么实现"变成"构造时明说"，
/// resolve<Target>() 上的约束换成纯类型匹配（`(same_as<Target, T> || ...)`），
/// 不涉及任何 D 的具体实现，是真正 SFINAE 友好、能拿来做重载决议依据的判据。
///
/// @pre dependency_construct<D, T...> 成立——T... 必须都是 D 真的支持的类型，
/// 这条前提本身没法在这里机械验证（原因同上），错的话会在真正 resolve 到
/// D 内部时才暴露成一个硬编译错误。
///
/// 纯引用包装，没有所有权语义，拷贝/移动跟拷贝一个指针一样安全，不禁用。
template <class D, class... T>
struct forward_dependency_construct
{
	explicit forward_dependency_construct(D& inner) noexcept : _inner(&inner) {}

	template <class Target>
		requires (::std::same_as<Target, T> || ...)
	[[nodiscard]] Target& resolve()
	{
		return _inner->template resolve<Target>();
	}

	D* _inner;
};


/// 把若干个 forward_dependency_construct<D, T...> 拼成一个：不新造任何存储，
/// 纯粹多重继承起来，再用一条 pack 展开的 using 声明把各自的 resolve<Target>
/// 重载合并到同一张候选表里。
///
/// 之所以不用手写的 if constexpr 分派链：合并之后剩下的就是一次普通的重载
/// 决议——每个 Fwd::resolve 上 `(same_as<Target, T> || ...)` 这条约束是纯类型
/// 匹配，跟 D 的实现无关，真正 SFINAE 友好，重载决议自己就能挑出唯一认识
/// Target 的那个；如果两个 Fwd 的 T... 恰好有重叠（拼接前提是类型集合不
/// 重叠，这种情况本该避免），调用点会是一次二义性错误，而不是悄悄选中某一
/// 个——这正是我们想要的失败方式。
///
/// 构造函数直接搬进来（forward_dependency_construct 拷贝/移动跟拷贝一个
/// 指针一样安全），CTAD 从构造参数自动推出 Fwd...，调用点不用重复写模板
/// 参数列表。
template <class... Fwd>
struct cat_dependency_construct : Fwd...
{
	explicit cat_dependency_construct(Fwd... fwd) : Fwd(::std::move(fwd))... {}

	using Fwd::resolve...;
};
