#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>

#include <vulkan/vulkan.h>

#include "./expression.h"
#include "./reference.h"
#include "./storage.h"
#include "./vulkan_object.h"

namespace vkfu
{
/// What a caller has to enable before an object exists.
///
/// vk.xml records this against every type, so it does not have to be remembered:
/// `names` are the extensions that provide the object, and `core` is the API
/// version that promoted it (0 when it was never promoted). Either one suffices
/// -- an object with core == VK_API_VERSION_1_2 needs no extension string on a
/// 1.2 instance, and needs one of `names` below that.
template<class Tag>
struct vulkan_object_extensions
{
	constexpr static ::std::array<char const*, 0> names{};
	constexpr static ::std::uint32_t core = 0;
	constexpr static auto instance = false;
};

/// Which of the two extension lists a name belongs in.
///
/// Vulkan has two, and putting a name in the wrong one fails with
/// VK_ERROR_EXTENSION_NOT_PRESENT. The chain does not decide it: a device
/// creation chain legitimately reaches structures that instance extensions
/// provide -- VkDeviceGroupDeviceCreateInfo hangs off VkDeviceCreateInfo but
/// comes from VK_KHR_device_group_creation, which is an instance extension.
enum class extension_scope
{
	any,
	instance,
	device,
};

namespace extension_detail
{
// Every tag an evaluated expression reaches, in chain order.
//
// A pointer slot holds its own pNext chain rather than joining the parent's, so
// walking pNext alone would miss whatever is hanging in one -- and those
// structures need their extensions enabled just the same. Hence the recursion
// through reference_storage's slots.
template<class Storage>
struct chain_tags
{
	using type = ::std::tuple<expression_vulkan_tag_t<::std::remove_cvref_t<Storage>>>;
};

template<class... Storages>
struct chain_tags<basic_storage<Storages...>>
{
	using type = decltype(::std::tuple_cat(::std::declval<typename chain_tags<Storages>::type>()...));
};

template<class Native, class... Slots>
struct chain_tags<reference_storage<Native, Slots...>>
{
	using type = decltype(::std::tuple_cat(
		::std::declval<::std::tuple<expression_vulkan_tag_t<Native>>>(),
		::std::declval<typename chain_tags<typename Slots::storage_type>::type>()...));
};

// An unfilled slot reaches nothing.
template<>
struct chain_tags<absent_storage>
{
	using type = ::std::tuple<>;
};

template<class Expression>
using chain_tags_t = typename chain_tags<expression_storage_t<Expression>>::type;

template<::std::uint32_t ApiVersion, extension_scope Scope, class Tag>
consteval auto covered() noexcept -> bool
{
	constexpr auto core = vulkan_object_extensions<Tag>::core;
	if (core != 0 && core <= ApiVersion)
	{
		return true;
	}
	if constexpr (Scope == extension_scope::instance)
	{
		return !vulkan_object_extensions<Tag>::instance;
	}
	else if constexpr (Scope == extension_scope::device)
	{
		return vulkan_object_extensions<Tag>::instance;
	}
	else
	{
		return false;
	}
}

template<::std::uint32_t ApiVersion, extension_scope Scope, class... Tags>
consteval auto upper_bound() noexcept -> ::std::size_t
{
	::std::size_t total = 0;
	(
		[&]
		{
			if (!covered<ApiVersion, Scope, Tags>())
			{
				total += vulkan_object_extensions<Tags>::names.size();
			}
		}(),
		...
	);
	return total;
}

// Two passes, because the deduped size is needed as a template argument before
// the array can be built. Comparison is by string_view: two occurrences of the
// same extension name need not be the same pointer.
template<::std::uint32_t ApiVersion, extension_scope Scope, ::std::size_t Capacity, class... Tags>
consteval auto gather() noexcept -> ::std::pair<::std::array<char const*, Capacity>, ::std::size_t>
{
	auto collected = ::std::array<char const*, Capacity>{};
	::std::size_t size = 0;
	(
		[&]
		{
			if (covered<ApiVersion, Scope, Tags>())
			{
				return;
			}
			for (auto const* candidate : vulkan_object_extensions<Tags>::names)
			{
				for (::std::size_t index = 0; index != size; ++index)
				{
					if (::std::string_view{collected[index]} == ::std::string_view{candidate})
					{
						return;
					}
				}
				collected[size++] = candidate;
			}
		}(),
		...
	);
	return {collected, size};
}

template<::std::uint32_t ApiVersion, extension_scope Scope, class... Tags>
consteval auto dedupe() noexcept
{
	constexpr auto capacity = upper_bound<ApiVersion, Scope, Tags...>();
	constexpr auto gathered = gather<ApiVersion, Scope, capacity, Tags...>();
	auto result = ::std::array<char const*, gathered.second>{};
	for (::std::size_t index = 0; index != gathered.second; ++index)
	{
		result[index] = gathered.first[index];
	}
	return result;
}

template<::std::uint32_t ApiVersion, extension_scope Scope, class Tuple>
struct from_tuple;

template<::std::uint32_t ApiVersion, extension_scope Scope, class... Tags>
struct from_tuple<ApiVersion, Scope, ::std::tuple<Tags...>>
{
	constexpr static auto value = dedupe<ApiVersion, Scope, Tags...>();
};
}

/// The extensions every object in a chain needs, deduplicated, in chain order.
///
/// `ApiVersion` is what the instance or device was created with: an object that
/// core promoted at or below it contributes nothing. The default is 1.0, which
/// every Vulkan implementation has, so the answer is never optimistic -- raise it
/// and promoted objects drop out of the list.
template<
	class Expression,
	::std::uint32_t ApiVersion = VK_API_VERSION_1_0,
	extension_scope Scope = extension_scope::any>
inline constexpr auto required_extensions_v =
	extension_detail::from_tuple<ApiVersion, Scope, extension_detail::chain_tags_t<Expression>>::value;

/// Same thing, deduced from an expression rather than spelled as a type.
///
///     auto const chain = param::feature::core{...} | param::feature::ext::mesh_shader{...};
///     vkfu::create_device(physical_device, param::device{
///         .enabled_extension_names = vkfu::required_extensions(chain),
///         ...
///     } | chain);
template<
	::std::uint32_t ApiVersion = VK_API_VERSION_1_0,
	extension_scope Scope = extension_scope::any,
	expression Expression>
[[nodiscard]] consteval auto required_extensions(Expression const&) noexcept
{
	return required_extensions_v<Expression, ApiVersion, Scope>;
}

/// The two lists, separated. A device-creation chain can need both: enable the
/// instance ones at vkCreateInstance and the device ones at vkCreateDevice.
template<::std::uint32_t ApiVersion = VK_API_VERSION_1_0, expression Expression>
[[nodiscard]] consteval auto required_instance_extensions(Expression const&) noexcept
{
	return required_extensions_v<Expression, ApiVersion, extension_scope::instance>;
}

template<::std::uint32_t ApiVersion = VK_API_VERSION_1_0, expression Expression>
[[nodiscard]] consteval auto required_device_extensions(Expression const&) noexcept
{
	return required_extensions_v<Expression, ApiVersion, extension_scope::device>;
}
}
