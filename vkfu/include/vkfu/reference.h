#pragma once

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "./expression.h"
#include "./storage.h"

namespace vkfu
{
// Some create-infos reach a child structure through a pointer member rather
// than through pNext: VkGraphicsPipelineCreateInfo::pVertexInputState and its
// siblings. A slot like that can hold an expression, and then the parent's
// storage owns the evaluated child and points at it.

struct absent_storage
{};

// The default for an unfilled slot. The native pointer stays null.
struct absent_expression
{
	constexpr auto evaluate() const noexcept -> absent_storage
	{
		return {};
	}
};

template<class T>
using reference_storage_of_t = ::std::remove_cvref_t<decltype(vkfu::evaluate(::std::declval<T>()))>;

template<class T, class Tag>
concept reference_expression_for =
	::std::same_as<::std::remove_cvref_t<T>, absent_expression>
	|| (expression<T> && ::std::same_as<expression_vulkan_tag_t<T>, Tag>);

template<auto Member, class Storage>
struct reference_slot
{
	static constexpr auto member = Member;
	using storage_type = Storage;

	Storage storage{};
};

/// Storage for a native structure plus the children its pointer members name.
///
/// Like basic_storage this owns its children by value, so every copy and move
/// has to re-point the parent at the children it actually owns.
template<class Native, class... Slots>
struct reference_storage
{
	using self_type = reference_storage;
	using native_type = Native;
	using tuple_type = ::std::tuple<Slots...>;

	constexpr reference_storage()
		noexcept(::std::is_nothrow_default_constructible_v<Native> && ::std::is_nothrow_default_constructible_v<tuple_type>)
		: native{}, slots{}
	{}

	constexpr explicit reference_storage(Native value, typename Slots::storage_type... storages)
		: native(value), slots(Slots{::std::move(storages)}...)
	{
		_relink();
		vkfu::set_next(native, nullptr);
	}

	constexpr reference_storage(self_type const& other)
		: native(other.native), slots(other.slots)
	{
		_relink();
	}

	constexpr reference_storage(self_type&& other)
		: native(::std::move(other.native)), slots(::std::move(other.slots))
	{
		_relink();
	}

	constexpr auto operator=(self_type const& other) -> self_type&
	{
		native = other.native;
		slots = other.slots;
		_relink();
		return *this;
	}

	constexpr auto operator=(self_type&& other) -> self_type&
	{
		native = ::std::move(other.native);
		slots = ::std::move(other.slots);
		_relink();
		return *this;
	}

	constexpr decltype(auto) address() & noexcept(noexcept(vkfu::address(native)))
	{
		return vkfu::address(native);
	}

	constexpr decltype(auto) set_next(void const* next) & noexcept(noexcept(vkfu::set_next(native, next)))
	{
		return vkfu::set_next(native, next);
	}

	constexpr void _relink() noexcept
	{
		self_type::_relink(::std::index_sequence_for<Slots...>{});
	}

	template<::std::size_t... Indices>
	constexpr void _relink(::std::index_sequence<Indices...>) noexcept
	{
		(self_type::template _wire<Indices>(), ...);
	}

	template<::std::size_t Index>
	constexpr void _wire() noexcept
	{
		using slot_type = ::std::tuple_element_t<Index, tuple_type>;
		if constexpr (!::std::same_as<typename slot_type::storage_type, absent_storage>)
		{
			using pointer_type = ::std::remove_reference_t<decltype(native.*slot_type::member)>;
			native.*slot_type::member = static_cast<pointer_type>(vkfu::address(::std::get<Index>(slots).storage));
		}
	}

	native_type native;
	tuple_type slots;
};
}
