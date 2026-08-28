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

	/// The native structure a tag stands for, and its sType.
	///
	/// The opposite direction to expression_vulkan_tag: given only a tag, a query
	/// chain has to declare storage for the structure and stamp its sType before
	/// the driver ever sees it. The generated header specializes this for every
	/// object it knows.
	template<class Tag>
	struct vulkan_object_native
	{};

	template<class Tag>
		requires requires { typename vulkan_object_native<::std::remove_cvref_t<Tag>>::type; }
	using vulkan_object_native_t = typename vulkan_object_native<::std::remove_cvref_t<Tag>>::type;

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
