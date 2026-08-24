#include <cassert>
#include <cstddef>
#include <span>
#include <tuple>
#include <utility>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

namespace obj = vkfu::obj;
namespace param = vkfu::param;

static_assert(vkfu::vulkan_root_object<obj::device>);
static_assert(vkfu::vulkan_branch_object<obj::device>);
static_assert(vkfu::vulkan_branch_object<obj::feature::core>);
static_assert(vkfu::vulkan_branch_object<obj::feature::huawei::cluster_culling_shader>);
static_assert(vkfu::duplicatable_vulkan_object<obj::option::device_private_data>);
static_assert(!vkfu::duplicatable_vulkan_object<obj::feature::timeline_semaphore>);

static_assert(vkfu::vulkan_object_compatible_with<obj::device, obj::feature::core>);
static_assert(vkfu::vulkan_object_compatible_with<obj::feature::core, obj::feature::timeline_semaphore>);
static_assert(vkfu::vulkan_object_compatible_with<obj::feature::core, obj::feature::host_query_reset>);
static_assert(vkfu::vulkan_object_compatible_with<obj::feature::core, obj::feature::huawei::cluster_culling_shader>);
static_assert(vkfu::vulkan_object_compatible_with<obj::feature::huawei::cluster_culling_shader, obj::feature::huawei::cluster_culling_shader_vrs>);
static_assert(vkfu::vulkan_object_compatible_with<obj::device, obj::option::device_private_data>);
// vk.xml has no structextends edge here, so the pipe must not compile.
static_assert(!vkfu::vulkan_object_compatible_with<obj::feature::core, obj::option::device_private_data>);

static_assert(vkfu::expression<param::device>);
static_assert(vkfu::expression<param::feature::core>);
static_assert(vkfu::expression<param::feature::timeline_semaphore>);
static_assert(vkfu::expression<param::command_pool>);

static_assert(vkfu::storable<VkDeviceCreateInfo>);
static_assert(vkfu::storable<VkPhysicalDeviceFeatures2>);
static_assert(vkfu::storable<VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI>);

// The requirement has to be dependent: gcc treats an invalid expression in a
// non-template requires-expression as a hard error rather than as `false`.
template<class... Expressions>
concept pipeable = requires(Expressions... expressions) { (... | expressions); };

using nested_features = decltype(param::feature::core{} | param::feature::timeline_semaphore{});

// A non-repeatable feature may appear once per branch.
static_assert(pipeable<param::feature::core, param::feature::timeline_semaphore>);
static_assert(!pipeable<param::feature::core, param::feature::timeline_semaphore, param::feature::timeline_semaphore>);
// allow_duplicate lifts that restriction.
static_assert(pipeable<param::device, param::option::device_private_data, param::option::device_private_data>);
// The check is per level: the nested branch has its own feature list, so the
// same object may appear once inside it and once outside.
static_assert(pipeable<param::device, param::feature::timeline_semaphore, nested_features>);
// An object that does not extend the branch is rejected regardless.
static_assert(!pipeable<param::feature::core, param::option::device_private_data>);

template<class Storage>
void check_device_feature_chain(Storage& storage, VkDeviceQueueCreateInfo const* queue_info, char const* const* extensions)
{
	auto&& device = ::std::get<0>(storage.storages);
	auto&& features = ::std::get<1>(storage.storages);
	auto&& core = ::std::get<0>(features.storages);
	auto&& timeline = ::std::get<1>(features.storages);
	auto&& host_query = ::std::get<2>(features.storages);
	auto&& cluster = ::std::get<3>(features.storages);
	auto&& cluster_info = ::std::get<0>(cluster.storages);
	auto&& cluster_vrs_info = ::std::get<1>(cluster.storages);

	assert(device.queueCreateInfoCount == 1);
	assert(device.pQueueCreateInfos == queue_info);
	assert(device.enabledExtensionCount == 1);
	assert(device.ppEnabledExtensionNames == extensions);
	assert(device.pNext == vkfu::address(features));
	assert(core.pNext == vkfu::address(timeline));
	assert(timeline.timelineSemaphore == VK_TRUE);
	assert(timeline.pNext == vkfu::address(host_query));
	assert(host_query.hostQueryReset == VK_TRUE);
	assert(host_query.pNext == vkfu::address(cluster));
	assert(cluster_info.clustercullingShader == VK_TRUE);
	assert(cluster_info.multiviewClusterCullingShader == VK_FALSE);
	assert(cluster_info.pNext == vkfu::address(cluster_vrs_info));
	assert(cluster_vrs_info.clusterShadingRate == VK_TRUE);
	assert(cluster_vrs_info.pNext == nullptr);
}

void test_device_feature_chain()
{
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

	auto feature_expression = param::feature::core{}
		| param::feature::timeline_semaphore{.enable = true}
		| param::feature::host_query_reset{.enable = true}
		| (param::feature::huawei::cluster_culling_shader{.enable = true}
			| param::feature::huawei::cluster_culling_shader_vrs{.cluster_shading_rate = true});
	auto expression = param::device{
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
	auto expression = param::device{}
		| param::option::device_private_data{.private_data_slot_request_count = 2}
		| param::option::device_private_data{.private_data_slot_request_count = 5};
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
	auto state = param::option::khr::pipeline_fragment_shading_rate_state{
		.fragment_size = VkExtent2D{.width = 2, .height = 4},
		.combiner_ops = {VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MAX_KHR},
	};
	auto native = vkfu::evaluate(state);
	assert(native.sType == VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR);
	assert(native.pNext == nullptr);
	assert(native.fragmentSize.width == 2);
	assert(native.fragmentSize.height == 4);
	assert(native.combinerOps[0] == VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR);
	assert(native.combinerOps[1] == VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MAX_KHR);
}

void test_flag_bits()
{
	// The generated header already static_asserts that each bit-field sits on
	// the right bit; this only checks that a whole word round-trips.
	auto pool = param::command_pool{
		.flags = {.transient = 1, .protected_ = 1},
		.queue_family_index = 7,
	};
	auto native = vkfu::evaluate(pool);
	assert(native.flags == (VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_PROTECTED_BIT));
	assert(native.queueFamilyIndex == 7);

	auto empty = vkfu::evaluate(param::command_pool{});
	assert(empty.flags == 0);
}

int main()
{
	test_device_feature_chain();
	test_duplicatable_object();
	test_fixed_arrays();
	test_flag_bits();
}
