#pragma once

#include <cstdint>
#include <span>
#include <utility>

#include <vulkan/vulkan.h>

#include "./expression.hpp"

namespace vkfu
{
struct device_object final : vulkan_object_base
{};

struct feature2_object final : vulkan_object_base
{};

struct timeline_semaphore_object final : vulkan_object_base
{};

struct host_query_reset_object final : vulkan_object_base
{};

struct cluster_culling_object final : vulkan_object_base
{};

struct cluster_culling_vrs_object final : vulkan_object_base
{};

struct allocator_object final : vulkan_object_base
{};

template<>
struct object_traits<device_object>
{
	inline static constexpr bool participates_in_pnext = true;
};

template<>
struct object_traits<feature2_object>
{
	inline static constexpr bool participates_in_pnext = true;
};

template<>
struct object_traits<timeline_semaphore_object>
{
	inline static constexpr bool participates_in_pnext = true;
};

template<>
struct object_traits<host_query_reset_object>
{
	inline static constexpr bool participates_in_pnext = true;
};

template<>
struct object_traits<cluster_culling_object>
{
	inline static constexpr bool participates_in_pnext = true;
};

template<>
struct object_traits<cluster_culling_vrs_object>
{
	inline static constexpr bool participates_in_pnext = true;
};

template<>
struct object_traits<allocator_object>
{
	inline static constexpr bool participates_in_pnext = false;
};

struct device_param
{
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDeviceCreateFlags flags{};
	::std::span<VkDeviceQueueCreateInfo const> queue_create_infos{};
	::std::span<char const* const> enabled_extensions{};
};

struct allocator_param
{
	VkAllocationCallbacks const* callbacks = nullptr;
};

struct feature2_param
{
	VkPhysicalDeviceFeatures features{};
};

struct timeline_semaphore_param
{
	bool timeline_semaphore = false;
};

struct host_query_reset_param
{
	bool host_query_reset = false;
};

struct cluster_culling_param
{
	bool cluster_culling_shader = false;
	bool multiview_cluster_culling_shader = false;
};

struct cluster_culling_vrs_param
{
	bool cluster_culling_vrs = false;
};

template<>
struct parameter_traits<device_param>
{
	using object_type = device_object;
	using native_type = VkDeviceCreateInfo;
	inline static constexpr expression_category category = expression_category::root;

	static auto make_native(device_param const& parameter) noexcept -> native_type
	{
		return VkDeviceCreateInfo{
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = nullptr,
			.flags = parameter.flags,
			.queueCreateInfoCount = static_cast<::std::uint32_t>(
				parameter.queue_create_infos.size()),
			.pQueueCreateInfos = parameter.queue_create_infos.data(),
			.enabledLayerCount = 0,
			.ppEnabledLayerNames = nullptr,
			.enabledExtensionCount = static_cast<::std::uint32_t>(
				parameter.enabled_extensions.size()),
			.ppEnabledExtensionNames = parameter.enabled_extensions.data(),
			.pEnabledFeatures = nullptr,
		};
	}

	static void set_pnext(native_type& native, void* next) noexcept
	{
		native.pNext = next;
	}
};

template<>
struct parameter_traits<allocator_param>
{
	using object_type = allocator_object;
	using native_type = detail::no_native;
	inline static constexpr expression_category category = expression_category::feature;

	static auto make_native(allocator_param const&) noexcept -> native_type
	{
		return {};
	}
};

template<>
struct parameter_traits<feature2_param>
{
	using object_type = feature2_object;
	using native_type = VkPhysicalDeviceFeatures2;
	inline static constexpr expression_category category = expression_category::branch;

	static auto make_native(feature2_param const& parameter) noexcept -> native_type
	{
		return VkPhysicalDeviceFeatures2{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = nullptr,
			.features = parameter.features,
		};
	}

	static void set_pnext(native_type& native, void* next) noexcept
	{
		native.pNext = next;
	}
};

template<>
struct parameter_traits<timeline_semaphore_param>
{
	using object_type = timeline_semaphore_object;
	using native_type = VkPhysicalDeviceTimelineSemaphoreFeatures;
	inline static constexpr expression_category category = expression_category::feature;

	static auto make_native(timeline_semaphore_param const& parameter) noexcept -> native_type
	{
		return VkPhysicalDeviceTimelineSemaphoreFeatures{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
			.pNext = nullptr,
			.timelineSemaphore = parameter.timeline_semaphore ? VK_TRUE : VK_FALSE,
		};
	}

	static void set_pnext(native_type& native, void* next) noexcept
	{
		native.pNext = next;
	}
};

template<>
struct parameter_traits<host_query_reset_param>
{
	using object_type = host_query_reset_object;
	using native_type = VkPhysicalDeviceHostQueryResetFeatures;
	inline static constexpr expression_category category = expression_category::feature;

	static auto make_native(host_query_reset_param const& parameter) noexcept -> native_type
	{
		return VkPhysicalDeviceHostQueryResetFeatures{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
			.pNext = nullptr,
			.hostQueryReset = parameter.host_query_reset ? VK_TRUE : VK_FALSE,
		};
	}

	static void set_pnext(native_type& native, void* next) noexcept
	{
		native.pNext = next;
	}
};

template<>
struct parameter_traits<cluster_culling_param>
{
	using object_type = cluster_culling_object;
	using native_type = VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI;
	inline static constexpr expression_category category = expression_category::branch;

	static auto make_native(cluster_culling_param const& parameter) noexcept -> native_type
	{
		return VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_FEATURES_HUAWEI,
			.pNext = nullptr,
			.clustercullingShader = parameter.cluster_culling_shader ? VK_TRUE : VK_FALSE,
			.multiviewClusterCullingShader =
				parameter.multiview_cluster_culling_shader ? VK_TRUE : VK_FALSE,
		};
	}

	static void set_pnext(native_type& native, void* next) noexcept
	{
		native.pNext = next;
	}
};

template<>
struct parameter_traits<cluster_culling_vrs_param>
{
	using object_type = cluster_culling_vrs_object;
	using native_type = VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI;
	inline static constexpr expression_category category = expression_category::feature;

	static auto make_native(cluster_culling_vrs_param const& parameter) noexcept -> native_type
	{
		return VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_VRS_FEATURES_HUAWEI,
			.pNext = nullptr,
			.clusterShadingRate = parameter.cluster_culling_vrs ? VK_TRUE : VK_FALSE,
		};
	}

	static void set_pnext(native_type& native, void* next) noexcept
	{
		native.pNext = next;
	}
};

template<>
struct attachment_rule<device_object, feature2_object>
{
	inline static constexpr bool valid = true;
	inline static constexpr bool allow_duplicate = false;
};

template<>
struct attachment_rule<device_object, allocator_object>
{
	inline static constexpr bool valid = true;
	inline static constexpr bool allow_duplicate = false;
};

// vk.xml lists these feature structs as extending both
// VkPhysicalDeviceFeatures2 and VkDeviceCreateInfo.
template<>
struct attachment_rule<device_object, timeline_semaphore_object>
{
	inline static constexpr bool valid = true;
	inline static constexpr bool allow_duplicate = false;
};

template<>
struct attachment_rule<device_object, host_query_reset_object>
{
	inline static constexpr bool valid = true;
	inline static constexpr bool allow_duplicate = false;
};

template<>
struct attachment_rule<device_object, cluster_culling_object>
{
	inline static constexpr bool valid = true;
	inline static constexpr bool allow_duplicate = false;
};

template<>
struct attachment_rule<feature2_object, timeline_semaphore_object>
{
	inline static constexpr bool valid = true;
	inline static constexpr bool allow_duplicate = false;
};

template<>
struct attachment_rule<feature2_object, host_query_reset_object>
{
	inline static constexpr bool valid = true;
	inline static constexpr bool allow_duplicate = false;
};

template<>
struct attachment_rule<feature2_object, cluster_culling_object>
{
	inline static constexpr bool valid = true;
	inline static constexpr bool allow_duplicate = false;
};

template<>
struct attachment_rule<cluster_culling_object, cluster_culling_vrs_object>
{
	inline static constexpr bool valid = true;
	inline static constexpr bool allow_duplicate = false;
};

struct device_dispatch
{
	PFN_vkCreateDevice create = &::vkCreateDevice;
};

namespace detail
{
template<class Storage>
auto allocator_callbacks(Storage const& storage) noexcept -> VkAllocationCallbacks const*
{
	if constexpr (direct_child_count_v<allocator_object, Storage> == 0)
	{
		return nullptr;
	}
	else
	{
		return get_child<allocator_object>(storage).parameter().callbacks;
	}
}
}

template<class E>
concept device_expression =
	expression<E> &&
	::std::same_as<expression_result_t<E>, device_object> &&
	(expression_category_v<E> == expression_category::root);

template<device_expression E>
auto create_device(E&& expression_value, device_dispatch dispatch = {}) -> VkDevice
{
	decltype(auto) infos = evaluate(::std::forward<E>(expression_value));
	VkDevice handle = VK_NULL_HANDLE;
	VkResult const result = dispatch.create(
		infos.parameter().physical_device,
		::std::addressof(infos.native()),
		detail::allocator_callbacks(infos),
		::std::addressof(handle));
	if (result != VK_SUCCESS)
	{
		throw result;
	}
	return handle;
}
}
