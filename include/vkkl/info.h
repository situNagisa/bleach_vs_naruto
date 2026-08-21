#pragma once
#include <optional>
#include <span>
#include <tuple>

#include <vulkan/vulkan.h>

struct cluster_culling_vrs {};
struct cluster_culling_vrs_result
{
	using result_tag = void;
	template<class T>
	inline constexpr static auto field = &T::cluster_culling_vrs;

	bool value;
};

struct cluster_culling
{
	cluster_culling_vrs cluster_culling_vrs;
};

struct timeline_semaphore {};
struct timeline_semaphore_result
{
	using result_tag = void;
	template<class T>
	inline constexpr static auto field = &T::timeline_semaphore;

	bool value;
};

struct host_query_reset {};
struct host_query_reset_result
{
	using result_tag = void;
	template<class T>
	inline constexpr static auto field = &T::host_query_reset;
	bool value;
};

struct feature2
{
	timeline_semaphore timeline_semaphore;
	host_query_reset host_query_reset;
	cluster_culling cluster_culling;
};
struct feature2_result
{
	using result_tag = void;
	template<class T>
	inline constexpr static auto field = &T::feature2;
	timeline_semaphore_result timeline_semaphore;
	host_query_reset_result host_query_reset;
	cluster_culling_vrs_result cluster_culling_vrs;
};

struct device
{
	::std::optional<feature2> feature2;
};

template<class Dag>
constexpr Dag check_dag(auto&&... opts)
{
	// for example Dag = device
	// for each (opt in opts(feature2_result, allocator))
	//	if dag.*feature2_result::field<device> is not null
	//		ill-format
	//	dag.*feature2_result::field<device>
}


inline constexpr struct
{
	auto operator()(
		
		, auto&&... opts // allocators be contained
		)
	{
		// solve opts in consteval
		auto dag = ::check_dag<device>(::std::forward<decltype(opts)>(opts)...);
		// tuple<opts[0], opts[1], ...> chain
		::std::tuple<VkPhysicalDeviceFeatures2/*, allocator */> chain{};
		// construct chain[0] by opts[0] (feature2_result)
		// construct chain[1] by opts[1] (allocator)

		VkDeviceCreateInfo info{
			// .sType = auto,
			// .pNext = &chain[0],
			// .flags = flag,
			// .queueCreateInfoCount = queue_create_infos,
			// .pQueueCreateInfos = queue_create_infos,
			// .enabledLayerCount = ~,
			// .ppEnabledLayerNames = ~,
			// .enabledExtensionCount = enabled_extensions,
			// .ppEnabledExtensionNames = enabled_extensions,
			// .pEnabledFeatures = ~,
		};
		VkDevice handle;
		if (!vkCreateDevice(physical_device, &info, nullptr /*chain[1]*/, &handle))
		{
			throw;
		}
		return handle;
	}
}create_device{};