#pragma once

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "./storage.h"
#include "./vulkan_object.h"

namespace vkfu
{
/// A structure that may be hung off `Head`'s pNext when reading.
///
/// The same structextends edges the write side uses, read in the same direction:
/// the branch is the structure the command fills in, the extension is what the
/// caller wants filled in as well.
template<class Tag, class Head>
concept query_extension_of = vulkan_object_compatible_with<Head, Tag>;

namespace query_detail
{
template<class Tag, class... All>
consteval auto index_of() noexcept -> ::std::size_t
{
	auto index = ::std::size_t{0};
	auto found = sizeof...(All);
	(
		[&]
		{
			if (::std::same_as<Tag, All> && found == sizeof...(All))
			{
				found = index;
			}
			++index;
		}(),
		...
	);
	return found;
}

template<class... All>
consteval auto all_distinct() noexcept -> bool
{
	auto seen = ::std::size_t{0};
	return ((index_of<All, All...>() == seen++) && ...);
}
}

/// Storage for a pNext chain a command writes into.
///
/// The write side builds a chain out of expressions because the caller supplies
/// the values. Reading is the other way round: the caller supplies only the
/// shape, so all this has to do is own the structures, stamp every sType and
/// keep pNext pointing at its own members -- which is what basic_storage already
/// guarantees across a copy or a move.
///
///     auto properties = vkfu::get_physical_device_properties2<
///         obj::property::driver, obj::property::id>(physical_device);
///     properties.head().properties.limits.maxImageDimension2D;
///     properties.get<obj::property::driver>().driverName;
template<vulkan_object Head, query_extension_of<Head>... Tags>
	requires (query_detail::all_distinct<Head, Tags...>())
struct query_chain
{
	using tags_type = ::std::tuple<Head, Tags...>;
	using native_type = vulkan_object_native_t<Head>;
	using storage_type = basic_storage<native_type, vulkan_object_native_t<Tags>...>;

	constexpr query_chain() noexcept
	{
		// basic_storage has already linked pNext; sType is independent of it, and
		// the driver rejects the chain without it.
		_stamp(::std::make_index_sequence<1 + sizeof...(Tags)>{});
	}

	constexpr auto head() & noexcept -> native_type&
	{
		return ::std::get<0>(storage.storages);
	}

	constexpr auto head() const& noexcept -> native_type const&
	{
		return ::std::get<0>(storage.storages);
	}

	template<class Tag>
		requires (query_detail::index_of<Tag, Head, Tags...>() < 1 + sizeof...(Tags))
	constexpr auto get() & noexcept -> auto&
	{
		return ::std::get<query_detail::index_of<Tag, Head, Tags...>()>(storage.storages);
	}

	template<class Tag>
		requires (query_detail::index_of<Tag, Head, Tags...>() < 1 + sizeof...(Tags))
	constexpr auto get() const& noexcept -> auto const&
	{
		return ::std::get<query_detail::index_of<Tag, Head, Tags...>()>(storage.storages);
	}

	storage_type storage{};

private:
	template<::std::size_t... Indices>
	constexpr void _stamp(::std::index_sequence<Indices...>) noexcept
	{
		((::std::get<Indices>(storage.storages).sType =
			vulkan_object_native<::std::tuple_element_t<Indices, tags_type>>::structure_type),
			...);
	}
};
}
