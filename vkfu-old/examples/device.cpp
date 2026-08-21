#include "../include/vkfu/vkfu.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>

namespace
{
VkAllocationCallbacks const* expected_allocator = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL fake_vk_create_device(
	VkPhysicalDevice physical_device,
	VkDeviceCreateInfo const* info,
	VkAllocationCallbacks const* allocator,
	VkDevice* device)
{
	assert(physical_device != VK_NULL_HANDLE);
	assert(info != nullptr);
	assert(info->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
	assert(allocator == expected_allocator);

	constexpr ::std::array expected_chain{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_FEATURES_HUAWEI,
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_VRS_FEATURES_HUAWEI,
	};

	auto const* current = static_cast<VkBaseInStructure const*>(info->pNext);
	for (VkStructureType const expected : expected_chain)
	{
		assert(current != nullptr);
		assert(current->sType == expected);
		current = current->pNext;
	}
	assert(current == nullptr);

	*device = reinterpret_cast<VkDevice>(::std::uintptr_t{0xC0FFEE});
	return VK_SUCCESS;
}
}

int main()
{
	using namespace vkfu;

	float queue_priority = 1.0F;
	VkDeviceQueueCreateInfo queue_info{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.queueFamilyIndex = 0,
		.queueCount = 1,
		.pQueuePriorities = &queue_priority,
	};
	char const* extensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	VkAllocationCallbacks callbacks{};

	device_param device_parameters{
		.physical_device = reinterpret_cast<VkPhysicalDevice>(::std::uintptr_t{0x1234}),
		.flags = 0,
		.queue_create_infos = ::std::span{&queue_info, 1u},
		.enabled_extensions = extensions,
	};

	auto explicit_feature2 =
		branch(feature2_param{})
		| feature(timeline_semaphore_param{.timeline_semaphore = true})
		| feature(host_query_reset_param{.host_query_reset = true})
		| feature(
			branch(cluster_culling_param{
				.cluster_culling_shader = true,
				.multiview_cluster_culling_shader = false})
			| feature(cluster_culling_vrs_param{.cluster_culling_vrs = true}));

	auto concise_feature2 =
		feature2_param{}
		| timeline_semaphore_param{.timeline_semaphore = true}
		| host_query_reset_param{.host_query_reset = true}
		| (cluster_culling_param{
			.cluster_culling_shader = true,
			.multiview_cluster_culling_shader = false}
			| cluster_culling_vrs_param{.cluster_culling_vrs = true});

	static_assert(::std::same_as<
		decltype(explicit_feature2), decltype(concise_feature2)>);
	static_assert(expression<decltype(concise_feature2)>);
	static_assert(::std::same_as<
		expression_result_t<decltype(concise_feature2)>, feature2_object>);

	auto device_expression =
		root(device_parameters)
		| feature(concise_feature2)
		| feature(allocator_param{.callbacks = &callbacks});

	auto infos = evaluate(device_expression);
	static_assert(expression<decltype(infos)>);
	assert(::std::addressof(evaluate(infos)) == ::std::addressof(infos));
	assert(infos.native().pQueueCreateInfos == &queue_info);
	assert(infos.native().ppEnabledExtensionNames == extensions);

	// Copy/move rebuild only the internally owned pNext links. The spans still
	// point at the caller-owned arrays above.
	auto copied_infos = infos;
	auto moved_infos = ::std::move(copied_infos);
	assert(moved_infos.native().pNext != infos.native().pNext);
	assert(moved_infos.native().pQueueCreateInfos == &queue_info);

	// A named evaluated branch is borrowed by reference.
	auto evaluated_feature2 = evaluate(concise_feature2);
	auto borrowed_infos = evaluate(device_parameters | evaluated_feature2);
	assert(borrowed_infos.native().pNext == evaluated_feature2.native_address());

	// An evaluated temporary is moved into and owned by the parent storage.
	auto owned_infos = evaluate(
		device_parameters | evaluate(concise_feature2));
	assert(owned_infos.native().pNext != evaluated_feature2.native_address());

	expected_allocator = &callbacks;
	VkDevice const handle = create_device(
		moved_infos, device_dispatch{.create = &fake_vk_create_device});
	assert(handle == reinterpret_cast<VkDevice>(::std::uintptr_t{0xC0FFEE}));

	::std::cout << "vkfu expression evaluated; pNext chain verified\n";
}
