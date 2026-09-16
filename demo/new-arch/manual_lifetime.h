#pragma once

#include <memory>
#include <new>
#include <type_traits>

/// 手工管理生命周期的存储槽：构造、析构、取用都得自己调，不按常规生命周期规则走。
///
/// op-state 是地址敏感的（子接收者持有指向它的指针），既不可拷贝也不可移动，于是
/// `::std::optional::emplace` 这条路走不通——它是直接初始化，会去找移动构造。
/// 这里走 `::new (地址) T(工厂())`：工厂返回 T 的纯右值，C++17 保证省略。
///
/// **槽里不记"有没有东西"**。那个 bool 按对齐摊下来每槽要多占一整个对齐单位，
/// N 个孩子就是 N 份。已构造到第几个由持有者用一个计数统一记，代价是持有者
/// 得自己写 try/catch 回滚——见 `dynamic_when_all_sender::operation_type::_constructed`。
///
/// 实现一比一对照 `::stdexec::__manual_lifetime`，只差一处：那边写了个空的用户析构函数
/// 来挂注释，代价是类型变成非平凡析构，`delete[]` 要挨个走一遍空析构。这里不声明析构函数
/// ——隐式析构本来就是平凡的、本来就什么都不做，语义完全一致。
///
/// 构造函数同样什么都不做：想让 `ValueType` 的生命周期开始得自己调 `construct`；
/// 想结束得自己调 `destroy`。
template <class ValueType>
struct manual_lifetime
{
	constexpr manual_lifetime() noexcept = default;

	manual_lifetime(manual_lifetime&&) = delete;
	auto operator=(manual_lifetime&&) -> manual_lifetime& = delete;

	/// 就地构造。不检查槽里是不是已经有东西了。
	template <class... Args>
	constexpr auto construct(Args&&... arguments)
		noexcept(::std::is_nothrow_constructible_v<ValueType, Args...>) -> ValueType&
	{
		// 用 placement new 而不是 `::std::construct_at`：前者支持聚合初始化的花括号省略。
		return *::std::launder(
			::new (static_cast<void*>(_buffer)) ValueType{ static_cast<Args&&>(arguments)... });
	}

	/// 用 `function(arguments...)` 的返回值就地构造。不检查槽里是不是已经有东西了。
	template <class Function, class... Args>
	constexpr auto construct_from(Function&& function, Args&&... arguments) -> ValueType&
	{
		// 同样用 placement new：返回值可能是不可移动的类型（op-state 正是），
		// `::std::construct_at` 那条路会去找移动构造。
		return *::std::launder(::new (static_cast<void*>(_buffer))
			ValueType{ static_cast<Function&&>(function)(static_cast<Args&&>(arguments)...) });
	}

	/// 结束里面那个 `ValueType` 的生命周期。\pre 生命周期已经开始。
	constexpr auto destroy() noexcept -> void { ::std::destroy_at(&get()); }

	/// \pre 生命周期已经开始。
	[[nodiscard]] constexpr auto get() & noexcept -> ValueType&
	{
		return *reinterpret_cast<ValueType*>(_buffer);
	}

	/// \pre 生命周期已经开始。
	[[nodiscard]] constexpr auto get() && noexcept -> ValueType&&
	{
		return static_cast<ValueType&&>(*reinterpret_cast<ValueType*>(_buffer));
	}

	/// \pre 生命周期已经开始。
	[[nodiscard]] constexpr auto get() const& noexcept -> ValueType const&
	{
		return *reinterpret_cast<ValueType const*>(_buffer);
	}

	constexpr auto get() const&& noexcept -> ValueType const&& = delete;

	[[nodiscard]] constexpr auto operator->() noexcept -> ValueType*
	{
		return reinterpret_cast<ValueType*>(_buffer);
	}

	[[nodiscard]] constexpr auto operator->() const noexcept -> ValueType const*
	{
		return reinterpret_cast<ValueType const*>(_buffer);
	}

	alignas(ValueType) unsigned char _buffer[sizeof(ValueType)]{};
};

/// 引用特化：引用没有生命周期可言，退化成存一根指针。
template <class ReferenceType>
	requires ::std::is_reference_v<ReferenceType>
struct manual_lifetime<ReferenceType>
{
	constexpr manual_lifetime() noexcept = default;

	manual_lifetime(manual_lifetime&&) = delete;
	auto operator=(manual_lifetime&&) -> manual_lifetime& = delete;

	constexpr auto construct(ReferenceType reference) noexcept -> ReferenceType
	{
		_pointer = ::std::addressof(reference);
		return static_cast<ReferenceType>(*_pointer);
	}

	template <class Function, class... Args>
	constexpr auto construct_from(Function&& function, Args&&... arguments)
		noexcept(::std::is_nothrow_invocable_v<Function, Args...>) -> ReferenceType
	{
		decltype(auto) result = static_cast<Function&&>(function)(static_cast<Args&&>(arguments)...);
		static_assert(::std::is_reference_v<decltype(result)>, "结果必须是引用");
		_pointer = ::std::addressof(result);
		return static_cast<ReferenceType>(*_pointer);
	}

	constexpr auto destroy() noexcept -> void {}

	[[nodiscard]] constexpr auto get() const noexcept -> ReferenceType
	{
		return static_cast<ReferenceType>(*_pointer);
	}

	[[nodiscard]] constexpr auto operator->() const noexcept -> ::std::add_pointer_t<ReferenceType>
	{
		return _pointer;
	}

	::std::add_pointer_t<ReferenceType> _pointer = nullptr;
};

/// `void` 特化：什么都不存，调用 `construct_from` 时只是把函数跑一遍。
template <>
struct manual_lifetime<void>
{
	constexpr manual_lifetime() noexcept = default;

	manual_lifetime(manual_lifetime&&) = delete;
	auto operator=(manual_lifetime&&) -> manual_lifetime& = delete;

	template <class... Args>
	constexpr auto construct(Args&&...) noexcept -> void
	{
	}

	template <class Function, class... Args>
	constexpr auto construct_from(Function&& function, Args&&... arguments) noexcept -> void
	{
		static_cast<void>(static_cast<Function&&>(function)(static_cast<Args&&>(arguments)...));
	}

	constexpr auto destroy() noexcept -> void {}

	constexpr auto get() const noexcept -> void {}

	[[nodiscard]] constexpr auto operator->() const noexcept -> void* { return nullptr; }
};
