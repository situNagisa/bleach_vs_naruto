#pragma once

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

namespace vkfu
{
namespace storage_customization
{
void address();
void set_next();

enum class choice
{
	member,
	adl,
	none,
};

struct choice_result
{
	choice selected;
	bool nothrow;
};

template<class Storage>
consteval auto choose_address() noexcept -> choice_result
{
	if constexpr (requires { ::std::declval<Storage>().address(); })
	{
		return {choice::member, noexcept(::std::declval<Storage>().address())};
	}
	else if constexpr (requires { address(::std::declval<Storage>()); })
	{
		return {choice::adl, noexcept(address(::std::declval<Storage>()))};
	}
	else
	{
		return {choice::none, true};
	}
}

template<class Storage>
consteval auto choose_set_next() noexcept -> choice_result
{
	if constexpr (requires { ::std::declval<Storage>().set_next(::std::declval<void const*>()); })
	{
		return {choice::member, noexcept(::std::declval<Storage>().set_next(::std::declval<void const*>()))};
	}
	else if constexpr (requires { set_next(::std::declval<Storage>(), ::std::declval<void const*>()); })
	{
		return {choice::adl, noexcept(set_next(::std::declval<Storage>(), ::std::declval<void const*>()))};
	}
	else
	{
		return {choice::none, true};
	}
}

struct address_t
{
	constexpr decltype(auto) operator()(auto&& storage) const noexcept(choose_address<decltype(storage)>().nothrow)
		requires (choose_address<decltype(storage)>().selected != choice::none)
	{
		constexpr auto selected = choose_address<decltype(storage)>().selected;
		if constexpr (selected == choice::member)
		{
			return ::std::forward<decltype(storage)>(storage).address();
		}
		else
		{
			return address(::std::forward<decltype(storage)>(storage));
		}
	}
};

struct set_next_t
{
	constexpr decltype(auto) operator()(auto&& storage, void const* next) const noexcept(choose_set_next<decltype(storage)>().nothrow)
		requires (choose_set_next<decltype(storage)>().selected != choice::none)
	{
		constexpr auto selected = choose_set_next<decltype(storage)>().selected;
		if constexpr (selected == choice::member)
		{
			return ::std::forward<decltype(storage)>(storage).set_next(next);
		}
		else
		{
			return set_next(::std::forward<decltype(storage)>(storage), next);
		}
	}
};
}

inline constexpr storage_customization::address_t address{};
inline constexpr storage_customization::set_next_t set_next{};

template<class T>
concept storable = ::std::semiregular<T> && requires(T& storage, void const* next)
{
	{ address(storage) } -> ::std::convertible_to<void const*>;
	set_next(storage, next);
};

template<storable... Storages>
	requires (sizeof...(Storages) != 0)
struct basic_storage
{
	using self_type = basic_storage;
	using tuple_type = ::std::tuple<Storages...>;

	constexpr explicit basic_storage(auto&&... values)
		noexcept(::std::is_nothrow_constructible_v<tuple_type, decltype(values)...> && noexcept(_relink()) && noexcept(set_next(nullptr)))
		requires (sizeof...(values) == sizeof...(Storages) && ::std::constructible_from<tuple_type, decltype(values)...>)
		: storages(::std::forward<decltype(values)>(values)...)
	{
		_relink();
		set_next(nullptr);
	}

	constexpr basic_storage() noexcept(::std::is_nothrow_default_constructible_v<tuple_type> && noexcept(_relink()) && noexcept(set_next(nullptr)))
		: storages{}
	{
		_relink();
		set_next(nullptr);
	}

	constexpr basic_storage(self_type const& other) noexcept(::std::is_nothrow_copy_constructible_v<tuple_type> && noexcept(_relink()))
		: storages(other.storages)
	{
		_relink();
	}

	constexpr basic_storage(self_type&& other) noexcept(::std::is_nothrow_move_constructible_v<tuple_type> && noexcept(_relink()))
		: storages(::std::move(other.storages))
	{
		_relink();
	}

	constexpr auto operator=(self_type const& other) noexcept(::std::is_nothrow_copy_assignable_v<tuple_type> && noexcept(_relink())) -> self_type&
	{
		storages = other.storages;
		_relink();
		return *this;
	}

	constexpr auto operator=(self_type&& other) noexcept(::std::is_nothrow_move_assignable_v<tuple_type> && noexcept(_relink())) -> self_type&
	{
		storages = ::std::move(other.storages);
		_relink();
		return *this;
	}

	constexpr decltype(auto) address() & noexcept(noexcept(vkfu::address(::std::get<0>(storages))))
	{
		return vkfu::address(::std::get<0>(storages));
	}

	constexpr decltype(auto) set_next(void const* next) & noexcept(noexcept(vkfu::set_next(::std::get<sizeof...(Storages) - 1>(storages), next)))
	{
		return vkfu::set_next(::std::get<sizeof...(Storages) - 1>(storages), next);
	}

	constexpr void _relink() noexcept(noexcept(self_type::_relink(::std::make_index_sequence<sizeof...(Storages) - 1>{})))
	{
		self_type::_relink(::std::make_index_sequence<sizeof...(Storages) - 1>{});
	}

	template<::std::size_t... Indices>
	constexpr void _relink(::std::index_sequence<Indices...>) noexcept((noexcept(vkfu::set_next(::std::get<Indices>(storages), vkfu::address(::std::get<Indices + 1>(storages)))) && ...))
	{
		(vkfu::set_next(::std::get<Indices>(storages), vkfu::address(::std::get<Indices + 1>(storages))), ...);
	}

	tuple_type storages;
};

template<class... Storages>
	requires (sizeof...(Storages) != 0 && (storable<::std::remove_cvref_t<Storages>> && ...))
basic_storage(Storages&&...) -> basic_storage<::std::remove_cvref_t<Storages>...>;


template<class... S>
void _match_basic_storage(basic_storage<S...> const&);

template<class T>
concept _derived_from_basic_storage = requires(T const& s)
{
	_match_basic_storage(s);
};

constexpr auto&& unpack(auto&& storage) noexcept
{
	if constexpr (_derived_from_basic_storage<decltype(storage)>)
		return ::std::get<0>(::std::forward<decltype(storage)>(storage).storages);
	else
		return ::std::forward<decltype(storage)>(storage);
}

}
