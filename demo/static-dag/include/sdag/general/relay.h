#pragma once

/// 接力存储：一组类型轮流占用同一块内存，任一时刻至多一个在世（前一个析构之后后一个才构造）。
///
///   relay<Ts...>
///     emplace<K>(args...)   构造第 K 个类型（调用方保证此刻没有别的在世）
///     get<K>()              第 K 个类型的对象（调用方保证它在世）
///     reset<K>()            析构第 K 个类型（不在世时什么都不做）
///
/// 全部类型都平凡析构时只是一个 union，大小 = 最大的那个；否则多一个字节记录谁在世，析构时据此析构它。
/// 构造 / 析构用 std::construct_at / std::destroy_at，常量求值里也能切换在世的成员。

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include "../config.h"
#include "tuple.h"

namespace sdag::general
{

namespace relay_detail
{

/// 递归 union：第 0 个成员是 head，其余在 tail 里。成员的构造与析构都由外层显式完成。
template<class... Ts>
union overlay;

template<>
union overlay<>
{
	constexpr overlay() noexcept
	{
	}
};

template<class T, class... Ts>
union overlay<T, Ts...>
{
	T head;
	overlay<Ts...> tail;

	constexpr overlay() noexcept
	{
	}

	constexpr ~overlay()
		requires (::std::is_trivially_destructible_v<T> && (::std::is_trivially_destructible_v<Ts> && ...))
	= default;

	constexpr ~overlay()
	{
	}
};

template<::std::size_t K, class Overlay>
[[nodiscard]] constexpr auto& member(Overlay& overlay) noexcept
{
	if constexpr (K == 0)
	{
		return overlay.head;
	}
	else
	{
		return relay_detail::member<K - 1>(overlay.tail);
	}
}

/// 构造第 K 个成员。常量求值里只有直接构造成员才会让它成为活跃成员，所以沿 tail 一层层先激活外层 union。
template<::std::size_t K, class Overlay, class... Args>
constexpr auto& construct(Overlay& overlay, Args&&... args)
{
	if constexpr (K == 0)
	{
		return *::std::construct_at(&overlay.head, ::std::forward<Args>(args)...);
	}
	else
	{
		::std::construct_at(&overlay.tail);
		return relay_detail::construct<K - 1>(overlay.tail, ::std::forward<Args>(args)...);
	}
}

}

template<class... Ts>
struct relay
{
	static constexpr bool trivial = (::std::is_trivially_destructible_v<Ts> && ...);

	relay_detail::overlay<Ts...> _storage;
	/// 在世的是第几个（从 1 开始），0 表示都不在世。全部平凡析构时不需要。
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<trivial, empty, unsigned char> _alive{};

	constexpr relay() noexcept = default;
	relay(relay const&) = delete;
	relay& operator=(relay const&) = delete;

	constexpr ~relay()
		requires trivial
	= default;

	constexpr ~relay()
	{
		[this]<::std::size_t... K>(::std::index_sequence<K...>)
		{
			(reset<K>(), ...);
		}(::std::index_sequence_for<Ts...>{});
	}

	template<::std::size_t K, class... Args>
	constexpr auto& emplace(Args&&... args)
		noexcept(::std::is_nothrow_constructible_v<Ts...[K], Args&&...>)
	{
		auto& object = relay_detail::construct<K>(_storage, ::std::forward<Args>(args)...);
		if constexpr (!trivial)
		{
			_alive = K + 1;
		}
		return object;
	}

	template<::std::size_t K>
	[[nodiscard]] constexpr auto& get() noexcept
	{
		return relay_detail::member<K>(_storage);
	}

	template<::std::size_t K>
	constexpr void reset() noexcept
	{
		if constexpr (!::std::is_trivially_destructible_v<Ts...[K]>)
		{
			if (_alive == K + 1)
			{
				::std::destroy_at(&relay_detail::member<K>(_storage));
				_alive = 0;
			}
		}
	}
};

}
