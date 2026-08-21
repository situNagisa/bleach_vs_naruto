#pragma once

#include <concepts>
#include <type_traits>

namespace vkfu
{
struct vulkan_object_base
{};

template<class T>
concept vulkan_object =
	::std::derived_from<::std::remove_cvref_t<T>, vulkan_object_base>;
}
