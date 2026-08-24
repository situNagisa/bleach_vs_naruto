#include <cassert>
#include <span>
#include <tuple>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

int main()
{
	using namespace vkfu;
	using namespace vkfu::param;

	auto queue_priority = 1.0f;
	auto queue_info = VkDeviceQueueCreateInfo{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.queueFamilyIndex = 0,
		.queueCount = 1,
		.pQueuePriorities = &queue_priority,
	};
	char const* extensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	auto features = feature::core{}
		| feature::timeline_semaphore{.enable = true}
		| feature::host_query_reset{.enable = true}
		| (feature::huawei::cluster_culling_shader{.enable = true}
			| feature::huawei::cluster_culling_shader_vrs{.cluster_shading_rate = true});
	auto expression = device{
		.queue_create_infos = ::std::span{&queue_info, 1u},
		.enabled_extension_names = extensions,
	} | features;

	auto infos = evaluate(expression);
	auto&& device_info = ::std::get<0>(infos.storages);
	auto&& feature_infos = ::std::get<1>(infos.storages);
	assert(device_info.pQueueCreateInfos == &queue_info);
	assert(device_info.ppEnabledExtensionNames == extensions);
	assert(device_info.pNext == vkfu::address(feature_infos));
}
