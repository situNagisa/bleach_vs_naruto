#pragma once

#include <cstddef>
#include <cassert>
#include <memory>
#include <utility>

/// 存放不可移动对象的手工生命周期槽。
///
/// op-state 是地址敏感的（子接收者持有指向它的指针），既不可拷贝也不可移动，于是
/// `::std::optional::emplace` 这条路走不通——它是直接初始化，会去找移动构造。
/// 这里走 `::new (地址) T(工厂())`：工厂返回 T 的纯右值，C++17 保证省略。
template <class value_type>
struct manual_lifetime
{
	alignas(value_type) ::std::byte _storage[sizeof(value_type)];
	bool _engaged = false;

	manual_lifetime() = default;
	manual_lifetime(manual_lifetime const&) = delete;
	auto operator=(manual_lifetime const&) -> manual_lifetime & = delete;

	~manual_lifetime()
	{
		reset();
	}

	template <class factory_type>
	auto construct(factory_type&& factory) -> value_type&
	{
		assert(!_engaged);
		auto const object = ::new (static_cast<void*>(_storage)) value_type(::std::forward<factory_type>(factory)());
		_engaged = true;
		return *object;
	}

	auto reset() noexcept -> void
	{
		if (_engaged)
		{
			_engaged = false;
			::std::launder(reinterpret_cast<value_type*>(_storage))->~value_type();
		}
	}

	[[nodiscard]] auto get() noexcept -> value_type&
	{
		assert(_engaged);
		return *::std::launder(reinterpret_cast<value_type*>(_storage));
	}
};