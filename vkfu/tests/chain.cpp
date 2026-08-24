// vkfu::chain builds in one step what `operator|` builds one binary expression
// at a time, and rejects exactly the same things.

#include <cassert>
#include <concepts>
#include <span>
#include <tuple>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

namespace param = vkfu::param;

// Same type as the fold, for rvalues and for lvalues alike.
static_assert(::std::same_as<
	decltype(vkfu::chain(
		param::feature::core{},
		param::feature::timeline_semaphore{},
		param::feature::host_query_reset{})),
	decltype(param::feature::core{}
		| param::feature::timeline_semaphore{}
		| param::feature::host_query_reset{})>);

// Chaining nothing is the branch itself.
static_assert(::std::same_as<decltype(vkfu::chain(param::feature::core{})), param::feature::core&&>);

template<class... Expressions>
concept chainable = requires(Expressions... expressions) { vkfu::chain(expressions...); };
template<class... Expressions>
concept pipeable = requires(Expressions... expressions) { (... | expressions); };

// The two agree on what is allowed.
static_assert(chainable<param::feature::core, param::feature::timeline_semaphore>);
static_assert(pipeable<param::feature::core, param::feature::timeline_semaphore>);

// A duplicate of a non-repeatable object is rejected, seen in one pass.
static_assert(!chainable<param::feature::core, param::feature::timeline_semaphore, param::feature::timeline_semaphore>);
static_assert(!pipeable<param::feature::core, param::feature::timeline_semaphore, param::feature::timeline_semaphore>);

// allow_duplicate lifts it for both.
static_assert(chainable<param::device, param::option::device_private_data, param::option::device_private_data>);
static_assert(pipeable<param::device, param::option::device_private_data, param::option::device_private_data>);

// An object that does not extend the branch is rejected by both.
static_assert(!chainable<param::feature::core, param::option::device_private_data>);
static_assert(!pipeable<param::feature::core, param::option::device_private_data>);

// Appending to a branch that already carries features also sees the whole set.
using two = decltype(param::feature::core{} | param::feature::timeline_semaphore{});
static_assert(chainable<two, param::feature::host_query_reset>);
static_assert(!chainable<two, param::feature::timeline_semaphore>);

int main()
{
	auto queue_priority = 1.0f;
	auto queue_info = VkDeviceQueueCreateInfo{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.queueFamilyIndex = 1,
		.queueCount = 1,
		.pQueuePriorities = &queue_priority,
	};

	auto features = vkfu::chain(
		param::feature::core{},
		param::feature::timeline_semaphore{.enable = true},
		param::feature::host_query_reset{.enable = true});
	auto storage = vkfu::evaluate(vkfu::chain(
		param::device{.queue_create_infos = ::std::span{&queue_info, 1u}},
		features));

	auto&& device_info = ::std::get<0>(storage.storages);
	auto&& feature_chain = ::std::get<1>(storage.storages);
	auto&& core = ::std::get<0>(feature_chain.storages);
	auto&& timeline = ::std::get<1>(feature_chain.storages);
	auto&& host_query = ::std::get<2>(feature_chain.storages);

	assert(device_info.queueCreateInfoCount == 1);
	assert(device_info.pNext == vkfu::address(feature_chain));
	assert(core.pNext == vkfu::address(timeline));
	assert(timeline.timelineSemaphore == VK_TRUE);
	assert(timeline.pNext == vkfu::address(host_query));
	assert(host_query.hostQueryReset == VK_TRUE);
	assert(host_query.pNext == nullptr);

	// Appending to an existing chain in one step.
	auto extended = vkfu::chain(
		param::feature::core{} | param::feature::timeline_semaphore{.enable = true},
		param::feature::host_query_reset{.enable = true});
	auto extended_storage = vkfu::evaluate(extended);
	assert(::std::tuple_size_v<decltype(extended_storage.storages)> == 3);
}
