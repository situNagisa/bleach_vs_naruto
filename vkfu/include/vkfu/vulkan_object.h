#pragma once

#include <type_traits>

namespace vkfu
{
	template<class T>
	struct vulkan_object_trait
	{
		constexpr static auto root = false;
		constexpr static auto branch = false;
		constexpr static auto allow_duplicate = false;
	};

	template<class T>
	concept vulkan_object = true;

	template<class T>
	concept vulkan_branch_object = vulkan_object<T> && requires { requires vulkan_object_trait<::std::remove_cvref_t<T>>::branch; };

	template<class T>
	concept vulkan_root_object = vulkan_object<T> && requires { requires vulkan_object_trait<::std::remove_cvref_t<T>>::root; };

	template<class T>
	concept duplicatable_vulkan_object = vulkan_object<T> && requires { requires vulkan_object_trait<::std::remove_cvref_t<T>>::allow_duplicate; };

	template<vulkan_branch_object Branch, vulkan_object Object>
	inline constexpr auto is_vulkan_object_compatible_with_v = false;

	template<class B, class O>
	concept vulkan_object_compatible_with = vulkan_object<B> && vulkan_object<O> && is_vulkan_object_compatible_with_v<::std::remove_cvref_t<B>, ::std::remove_cvref_t<O>>;
}
