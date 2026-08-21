#include <cassert>
#include <cstddef>
#include <span>
#include <tuple>
#include <utility>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

static_assert(vkfu::vulkan_root_object<vkfu::obj::device>);
static_assert(vkfu::vulkan_branch_object<vkfu::obj::device>);
static_assert(vkfu::vulkan_branch_object<vkfu::obj::feature2>);
static_assert(vkfu::vulkan_branch_object<vkfu::obj::cluster_culling>);
static_assert(vkfu::duplicatable_vulkan_object<vkfu::obj::device_private_data>);
static_assert(vkfu::vulkan_root_object<vkfu::obj::property2>);
static_assert(vkfu::vulkan_branch_object<vkfu::obj::property2>);

static_assert(vkfu::vulkan_object_compatible_with<vkfu::obj::device, vkfu::obj::feature2>);
static_assert(vkfu::vulkan_object_compatible_with<vkfu::obj::feature2, vkfu::obj::timeline_semaphore>);
static_assert(vkfu::vulkan_object_compatible_with<vkfu::obj::feature2, vkfu::obj::host_query_reset>);
static_assert(vkfu::vulkan_object_compatible_with<vkfu::obj::feature2, vkfu::obj::cluster_culling>);
static_assert(vkfu::vulkan_object_compatible_with<vkfu::obj::cluster_culling, vkfu::obj::cluster_culling_vrs>);
static_assert(vkfu::vulkan_object_compatible_with<vkfu::obj::device, vkfu::obj::device_private_data>);
static_assert(vkfu::vulkan_object_compatible_with<vkfu::obj::property2, vkfu::obj::id>);
static_assert(!vkfu::vulkan_object_compatible_with<vkfu::obj::feature2, vkfu::obj::id>);

static_assert(vkfu::expression<vkfu::param::device>);
static_assert(vkfu::expression<vkfu::param::feature2>);
static_assert(vkfu::expression<vkfu::param::timeline_semaphore>);
static_assert(vkfu::expression<vkfu::param::host_query_reset>);
static_assert(vkfu::expression<vkfu::param::cluster_culling>);
static_assert(vkfu::expression<vkfu::param::cluster_culling_vrs>);
static_assert(vkfu::expression<vkfu::param::device_private_data>);
static_assert(vkfu::expression<vkfu::param::property2>);
static_assert(vkfu::expression<vkfu::param::id>);

static_assert(vkfu::storable<VkDeviceCreateInfo>);
static_assert(vkfu::storable<VkPhysicalDeviceFeatures2>);
static_assert(vkfu::storable<VkPhysicalDeviceTimelineSemaphoreFeatures>);
static_assert(vkfu::storable<VkPhysicalDeviceHostQueryResetFeatures>);
static_assert(vkfu::storable<VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI>);
static_assert(vkfu::storable<VkPhysicalDeviceClusterCullingShaderVrsFeaturesHUAWEI>);
static_assert(vkfu::storable<VkDevicePrivateDataCreateInfo>);
static_assert(vkfu::storable<VkPhysicalDeviceProperties2>);
static_assert(vkfu::storable<VkPhysicalDeviceIDProperties>);

template<class Storage>
void check_device_feature_chain(Storage& storage, VkDeviceQueueCreateInfo const* queue_info, char const* const* extensions)
{
	auto&& device = ::std::get<0>(storage.storages);
	auto&& features = ::std::get<1>(storage.storages);
	auto&& features2 = ::std::get<0>(features.storages);
	auto&& timeline = ::std::get<1>(features.storages);
	auto&& host_query = ::std::get<2>(features.storages);
	auto&& cluster = ::std::get<3>(features.storages);
	auto&& cluster_culling_info = ::std::get<0>(cluster.storages);
	auto&& cluster_culling_vrs_info = ::std::get<1>(cluster.storages);

	assert(device.queueCreateInfoCount == 1);
	assert(device.pQueueCreateInfos == queue_info);
	assert(device.enabledExtensionCount == 1);
	assert(device.ppEnabledExtensionNames == extensions);
	assert(device.pNext == vkfu::address(features));
	assert(features2.pNext == vkfu::address(timeline));
	assert(timeline.timelineSemaphore == VK_TRUE);
	assert(timeline.pNext == vkfu::address(host_query));
	assert(host_query.hostQueryReset == VK_TRUE);
	assert(host_query.pNext == vkfu::address(cluster));
	assert(cluster_culling_info.clustercullingShader == VK_TRUE);
	assert(cluster_culling_info.multiviewClusterCullingShader == VK_FALSE);
	assert(cluster_culling_info.pNext == vkfu::address(cluster_culling_vrs_info));
	assert(cluster_culling_vrs_info.clusterShadingRate == VK_TRUE);
	assert(cluster_culling_vrs_info.pNext == nullptr);
}

void test_device_feature_chain()
{
	using namespace vkfu;
	using namespace vkfu::param;

	auto queue_priority = 1.0f;
	auto queue_info = VkDeviceQueueCreateInfo{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.queueFamilyIndex = 3,
		.queueCount = 1,
		.pQueuePriorities = &queue_priority,
	};
	char const* extensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	auto feature_expression = feature2{}
		| timeline_semaphore{.timeline_semaphore = true}
		| host_query_reset{.host_query_reset = true}
		| (cluster_culling{.cluster_culling_shader = true} | cluster_culling_vrs{.cluster_shading_rate = true});
	auto expression = device{
		.queue_create_infos = ::std::span{&queue_info, 1u},
		.enabled_extension_names = extensions,
	} | feature_expression;

	static_assert(vkfu::_derived_from_branch_pipe_expression<decltype(expression)>);
	static_assert(vkfu::branch_expression<decltype(expression)>);

	auto storage = vkfu::evaluate(expression);
	check_device_feature_chain(storage, &queue_info, extensions);

	auto copied = storage;
	check_device_feature_chain(copied, &queue_info, extensions);
	assert(vkfu::address(::std::get<1>(copied.storages)) != vkfu::address(::std::get<1>(storage.storages)));

	auto moved = ::std::move(copied);
	check_device_feature_chain(moved, &queue_info, extensions);
	assert(vkfu::address(::std::get<1>(moved.storages)) != vkfu::address(::std::get<1>(storage.storages)));
}

void test_duplicatable_object()
{
	using namespace vkfu;
	using namespace vkfu::param;

	auto expression = device{}
		| device_private_data{.private_data_slot_request_count = 2}
		| device_private_data{.private_data_slot_request_count = 5};
	auto storage = vkfu::evaluate(expression);
	auto&& device_info = ::std::get<0>(storage.storages);
	auto&& first = ::std::get<1>(storage.storages);
	auto&& second = ::std::get<2>(storage.storages);

	assert(device_info.pNext == vkfu::address(first));
	assert(first.privateDataSlotRequestCount == 2);
	assert(first.pNext == vkfu::address(second));
	assert(second.privateDataSlotRequestCount == 5);
	assert(second.pNext == nullptr);
}

void test_fixed_arrays()
{
	using namespace vkfu;
	using namespace vkfu::param;

	auto identifier = id{
		.device_node_mask = 7,
		.device_luid_valid = true,
	};
	for (::std::size_t index = 0; index < VK_UUID_SIZE; ++index)
	{
		identifier.device_uuid[index] = static_cast<::std::uint8_t>(index);
		identifier.driver_uuid[index] = static_cast<::std::uint8_t>(index + VK_UUID_SIZE);
	}
	for (::std::size_t index = 0; index < VK_LUID_SIZE; ++index)
	{
		identifier.device_luid[index] = static_cast<::std::uint8_t>(index + 1);
	}

	auto storage = vkfu::evaluate(property2{} | identifier);
	auto&& properties = ::std::get<0>(storage.storages);
	auto&& stored_id = ::std::get<1>(storage.storages);
	assert(properties.pNext == vkfu::address(stored_id));
	assert(stored_id.deviceNodeMask == 7);
	assert(stored_id.deviceLUIDValid == VK_TRUE);
	for (::std::size_t index = 0; index < VK_UUID_SIZE; ++index)
	{
		assert(stored_id.deviceUUID[index] == identifier.device_uuid[index]);
		assert(stored_id.driverUUID[index] == identifier.driver_uuid[index]);
	}
	for (::std::size_t index = 0; index < VK_LUID_SIZE; ++index)
	{
		assert(stored_id.deviceLUID[index] == identifier.device_luid[index]);
	}

	auto copied = storage;
	auto moved = ::std::move(copied);
	auto&& moved_properties = ::std::get<0>(moved.storages);
	auto&& moved_id = ::std::get<1>(moved.storages);
	assert(moved_properties.pNext == vkfu::address(moved_id));
	assert(moved_id.pNext == nullptr);
}

int main()
{
	test_device_feature_chain();
	test_duplicatable_object();
	test_fixed_arrays();
}
