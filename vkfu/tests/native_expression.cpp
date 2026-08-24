// A native Vulkan structure is itself an expression: it carries a vulkan tag
// through a trait specialization, and evaluating it is the identity.

#include <cassert>
#include <concepts>
#include <tuple>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

namespace param = vkfu::param;

static_assert(vkfu::expression<VkPhysicalDeviceTimelineSemaphoreFeatures>);
static_assert(::std::same_as<
	vkfu::expression_vulkan_tag_t<VkPhysicalDeviceTimelineSemaphoreFeatures>,
	vkfu::obj::feature::timeline_semaphore>);
static_assert(vkfu::branch_expression<VkPhysicalDeviceFeatures2>);
static_assert(::std::same_as<decltype(vkfu::evaluate(VkDeviceCreateInfo{})), VkDeviceCreateInfo>);

static_assert(vkfu::storable<VkDeviceCreateInfo>);
static_assert(vkfu::storable<VkPhysicalDeviceFeatures2>);

// The pNext hook is one overload per known structure, so a look-alike that vkfu
// was never told about is not claimed by it.
struct look_alike
{
	VkStructureType sType;
	void const* pNext;
};

static_assert(!vkfu::storable<look_alike>);
static_assert(!vkfu::expression<look_alike>);

int main()
{
	// A native structure mixes freely with param structures in a chain.
	auto native_timeline = VkPhysicalDeviceTimelineSemaphoreFeatures{};
	native_timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
	native_timeline.timelineSemaphore = VK_TRUE;

	auto chain = param::feature::core{}
		| native_timeline
		| param::feature::host_query_reset{.enable = true};

	auto storage = vkfu::evaluate(chain);
	auto&& core = ::std::get<0>(storage.storages);
	auto&& timeline = ::std::get<1>(storage.storages);
	auto&& host_query = ::std::get<2>(storage.storages);
	assert(core.pNext == vkfu::address(timeline));
	assert(timeline.timelineSemaphore == VK_TRUE);
	assert(timeline.pNext == vkfu::address(host_query));
	assert(host_query.hostQueryReset == VK_TRUE);
	assert(host_query.pNext == nullptr);

	// Evaluating an already-native value is the identity.
	auto raw = VkDeviceCreateInfo{};
	raw.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	raw.queueCreateInfoCount = 3;
	auto again = vkfu::evaluate(raw);
	assert(again.sType == raw.sType);
	assert(again.queueCreateInfoCount == 3);
}
