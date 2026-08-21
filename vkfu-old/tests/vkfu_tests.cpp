#include "../include/vkfu/vkfu.hpp"

#include <array>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace vkfu_test
{
struct repeat_root_object final : vkfu::vulkan_object_base
{};

struct repeat_feature_object final : vkfu::vulkan_object_base
{};

struct repeat_root_param
{};

struct repeat_feature_param
{
	int value = 0;
};

struct repeat_native
{
	void* pNext = nullptr;
	int value = 0;
};
}

namespace vkfu
{
template<>
struct object_traits<vkfu_test::repeat_root_object>
{
	inline static constexpr bool participates_in_pnext = true;
};

template<>
struct object_traits<vkfu_test::repeat_feature_object>
{
	inline static constexpr bool participates_in_pnext = true;
};

template<>
struct parameter_traits<vkfu_test::repeat_root_param>
{
	using object_type = vkfu_test::repeat_root_object;
	using native_type = vkfu_test::repeat_native;
	inline static constexpr expression_category category = expression_category::root;

	static auto make_native(vkfu_test::repeat_root_param const&) noexcept -> native_type
	{
		return {};
	}

	static void set_pnext(native_type& native, void* next) noexcept
	{
		native.pNext = next;
	}
};

template<>
struct parameter_traits<vkfu_test::repeat_feature_param>
{
	using object_type = vkfu_test::repeat_feature_object;
	using native_type = vkfu_test::repeat_native;
	inline static constexpr expression_category category = expression_category::feature;

	static auto make_native(vkfu_test::repeat_feature_param const& parameter) noexcept
		-> native_type
	{
		return {.pNext = nullptr, .value = parameter.value};
	}

	static void set_pnext(native_type& native, void* next) noexcept
	{
		native.pNext = next;
	}
};

template<>
struct attachment_rule<
	vkfu_test::repeat_root_object, vkfu_test::repeat_feature_object>
{
	inline static constexpr bool valid = true;
	inline static constexpr bool allow_duplicate = true;
};
}

namespace
{
using vkfu::operator|;

template<class L, class R>
concept pipeable = requires(L&& left, R&& right)
{
	::std::forward<L>(left) | ::std::forward<R>(right);
};

template<class E>
concept evaluatable = requires(E&& expression_value)
{
	vkfu::evaluate(::std::forward<E>(expression_value));
};

template<class E>
concept device_creatable = requires(E&& expression_value)
{
	vkfu::create_device(::std::forward<E>(expression_value));
};

using explicit_feature2_type = decltype(
	vkfu::branch(vkfu::feature2_param{})
	| vkfu::feature(vkfu::timeline_semaphore_param{})
	| vkfu::feature(vkfu::host_query_reset_param{})
	| vkfu::feature(
		vkfu::branch(vkfu::cluster_culling_param{})
		| vkfu::feature(vkfu::cluster_culling_vrs_param{})));

using concise_feature2_type = decltype(
	vkfu::feature2_param{}
	| vkfu::timeline_semaphore_param{}
	| vkfu::host_query_reset_param{}
	| (vkfu::cluster_culling_param{} | vkfu::cluster_culling_vrs_param{}));

static_assert(::std::same_as<explicit_feature2_type, concise_feature2_type>);
static_assert(vkfu::expression<vkfu::device_param>);
static_assert(vkfu::expression<concise_feature2_type>);
static_assert(vkfu::expression_category_v<vkfu::device_param> ==
	vkfu::expression_category::root);
static_assert(vkfu::expression_category_v<concise_feature2_type> ==
	vkfu::expression_category::branch);
static_assert(::std::same_as<
	vkfu::expression_result_t<concise_feature2_type>, vkfu::feature2_object>);

static_assert(vkfu::can_attach_v<vkfu::device_object, vkfu::feature2_object>);
static_assert(vkfu::can_attach_v<vkfu::device_object, vkfu::allocator_object>);
static_assert(vkfu::can_attach_v<
	vkfu::feature2_object, vkfu::timeline_semaphore_object>);
static_assert(vkfu::can_attach_v<
	vkfu::device_object, vkfu::timeline_semaphore_object>);
static_assert(!vkfu::can_attach_v<
	vkfu::feature2_object, vkfu::allocator_object>);

static_assert(pipeable<vkfu::device_param, vkfu::feature2_param>);
static_assert(pipeable<vkfu::device_param, vkfu::allocator_param>);
static_assert(pipeable<vkfu::feature2_param, vkfu::timeline_semaphore_param>);
static_assert(pipeable<vkfu::cluster_culling_param, vkfu::cluster_culling_vrs_param>);
static_assert(pipeable<vkfu::device_param, vkfu::timeline_semaphore_param>);
static_assert(!pipeable<vkfu::device_param, vkfu::cluster_culling_vrs_param>);
static_assert(!pipeable<vkfu::feature2_param, vkfu::allocator_param>);
static_assert(!pipeable<vkfu::timeline_semaphore_param, vkfu::host_query_reset_param>);
static_assert(!pipeable<vkfu::feature2_param, vkfu::device_param>);

using one_timeline_type = decltype(
	vkfu::feature2_param{} | vkfu::timeline_semaphore_param{});
static_assert(!pipeable<one_timeline_type, vkfu::timeline_semaphore_param>);
static_assert(evaluatable<vkfu::device_param>);
static_assert(evaluatable<vkfu::feature2_param>);
static_assert(!evaluatable<vkfu::timeline_semaphore_param>);
static_assert(evaluatable<concise_feature2_type>);
static_assert(!device_creatable<concise_feature2_type>);

using evaluated_feature2_type = vkfu::expression_storage_t<concise_feature2_type>;
static_assert(vkfu::expression<evaluated_feature2_type>);
static_assert(::std::same_as<
	decltype(vkfu::evaluate(::std::declval<evaluated_feature2_type&>())),
	evaluated_feature2_type&>);
static_assert(::std::same_as<
	decltype(vkfu::evaluate(::std::declval<evaluated_feature2_type const&>())),
	evaluated_feature2_type const&>);

using cluster_expression_type = decltype(
	vkfu::cluster_culling_param{} | vkfu::cluster_culling_vrs_param{});
using evaluated_cluster_type = vkfu::expression_storage_t<cluster_expression_type>;
using feature2_with_borrowed_cluster_type = decltype(
	vkfu::feature2_param{} | ::std::declval<evaluated_cluster_type&>());
static_assert(!pipeable<
	feature2_with_borrowed_cluster_type, vkfu::timeline_semaphore_param>);
static_assert(pipeable<
	feature2_with_borrowed_cluster_type, vkfu::allocator_param> == false);

using device_with_borrowed_feature2_type = decltype(
	vkfu::device_param{} | ::std::declval<evaluated_feature2_type&>());
static_assert(pipeable<device_with_borrowed_feature2_type, vkfu::allocator_param>);
static_assert(pipeable<
	vkfu::device_param, evaluated_feature2_type const&>);
static_assert(!pipeable<
	device_with_borrowed_feature2_type, vkfu::feature2_param>);

using repeat_once_type = decltype(
	vkfu_test::repeat_root_param{} | vkfu_test::repeat_feature_param{.value = 1});
static_assert(pipeable<repeat_once_type, vkfu_test::repeat_feature_param>);
using repeat_twice_type = decltype(
	vkfu_test::repeat_root_param{}
	| vkfu_test::repeat_feature_param{.value = 1}
	| vkfu_test::repeat_feature_param{.value = 2});
static_assert(vkfu::valid_expression_graph_v<repeat_twice_type>);

struct create_capture
{
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkAllocationCallbacks const* allocator = nullptr;
	::std::array<VkStructureType, 8> chain{};
	::std::size_t chain_size = 0;
	VkResult result = VK_SUCCESS;
};

create_capture capture;

VKAPI_ATTR VkResult VKAPI_CALL fake_create_device(
	VkPhysicalDevice physical_device,
	VkDeviceCreateInfo const* info,
	VkAllocationCallbacks const* allocator,
	VkDevice* device)
{
	capture.physical_device = physical_device;
	capture.allocator = allocator;
	capture.chain_size = 0;
	auto const* current = static_cast<VkBaseInStructure const*>(info->pNext);
	while (current != nullptr)
	{
		assert(capture.chain_size < capture.chain.size());
		capture.chain[capture.chain_size++] = current->sType;
		current = current->pNext;
	}
	*device = reinterpret_cast<VkDevice>(::std::uintptr_t{0xBADC0DE});
	return capture.result;
}

auto make_feature2_expression()
{
	return vkfu::feature2_param{}
		| vkfu::timeline_semaphore_param{.timeline_semaphore = true}
		| vkfu::host_query_reset_param{.host_query_reset = true}
		| (vkfu::cluster_culling_param{
			.cluster_culling_shader = true,
			.multiview_cluster_culling_shader = true}
			| vkfu::cluster_culling_vrs_param{.cluster_culling_vrs = true});
}

auto expected_chain()
{
	return ::std::array{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_FEATURES_HUAWEI,
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_VRS_FEATURES_HUAWEI,
	};
}

void verify_chain(VkDeviceCreateInfo const& info)
{
	auto const expected = expected_chain();
	auto const* current = static_cast<VkBaseInStructure const*>(info.pNext);
	for (auto const type : expected)
	{
		assert(current != nullptr);
		assert(current->sType == type);
		current = current->pNext;
	}
	assert(current == nullptr);
}

template<class Storage>
void verify_owned_links(Storage const& storage)
{
	auto const& feature2 = vkfu::get_child<vkfu::feature2_object>(storage);
	auto const& timeline =
		vkfu::get_child<vkfu::timeline_semaphore_object>(feature2);
	auto const& host_query =
		vkfu::get_child<vkfu::host_query_reset_object>(feature2);
	auto const& cluster =
		vkfu::get_child<vkfu::cluster_culling_object>(feature2);
	auto const& vrs =
		vkfu::get_child<vkfu::cluster_culling_vrs_object>(cluster);

	assert(storage.native().pNext == feature2.native_address());
	assert(feature2.native().pNext == timeline.native_address());
	assert(timeline.native().pNext == host_query.native_address());
	assert(host_query.native().pNext == cluster.native_address());
	assert(cluster.native().pNext == vrs.native_address());
	assert(vrs.native().pNext == nullptr);
}

template<class T>
auto move_out(T& source) -> T
{
	return T{::std::move(source)};
}
}

int main()
{
	using namespace vkfu;

	auto repeated = evaluate(
		vkfu_test::repeat_root_param{}
		| vkfu_test::repeat_feature_param{.value = 11}
		| vkfu_test::repeat_feature_param{.value = 22});
	auto const* first_repeat =
		static_cast<vkfu_test::repeat_native const*>(repeated.native().pNext);
	assert(first_repeat != nullptr);
	assert(first_repeat->value == 11);
	auto const* second_repeat =
		static_cast<vkfu_test::repeat_native const*>(first_repeat->pNext);
	assert(second_repeat != nullptr);
	assert(second_repeat->value == 22);
	assert(second_repeat->pNext == nullptr);

	float priority = 0.75F;
	VkDeviceQueueCreateInfo queues[]{
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.queueFamilyIndex = 7,
			.queueCount = 1,
			.pQueuePriorities = &priority,
		},
	};
	char const* extensions[]{"VK_FAKE_one", "VK_FAKE_two"};
	VkAllocationCallbacks allocator{};
	VkPhysicalDevice const physical_device =
		reinterpret_cast<VkPhysicalDevice>(::std::uintptr_t{0x12345});

	device_param const device_parameters{
		.physical_device = physical_device,
		.flags = 0,
		.queue_create_infos = queues,
		.enabled_extensions = extensions,
	};

	// Optional means structural absence: all of these are independently valid.
	auto empty_device = evaluate(device_parameters);
	assert(empty_device.native().pNext == nullptr);
	assert(empty_device.native().pQueueCreateInfos == queues);
	assert(empty_device.native().ppEnabledExtensionNames == extensions);

	auto full = evaluate(
		device_parameters | make_feature2_expression()
		| allocator_param{.callbacks = &allocator});
	verify_chain(full.native());
	verify_owned_links(full);
	assert(full.native().pQueueCreateInfos == queues);
	assert(full.native().ppEnabledExtensionNames == extensions);

	auto const& feature2 = get_child<feature2_object>(full);
	assert(feature2.native().features.robustBufferAccess == VK_FALSE);
	auto const& timeline = get_child<timeline_semaphore_object>(feature2);
	assert(timeline.native().timelineSemaphore == VK_TRUE);
	auto const& host_query = get_child<host_query_reset_object>(feature2);
	assert(host_query.native().hostQueryReset == VK_TRUE);
	auto const& cluster = get_child<cluster_culling_object>(feature2);
	assert(cluster.native().clustercullingShader == VK_TRUE);
	assert(cluster.native().multiviewClusterCullingShader == VK_TRUE);
	auto const& vrs = get_child<cluster_culling_vrs_object>(cluster);
	assert(vrs.native().clusterShadingRate == VK_TRUE);

	// Copy/move reconstruct all internal links but leave borrowed spans untouched.
	auto copied = full;
	assert(copied.native().pNext != full.native().pNext);
	assert(copied.native().pQueueCreateInfos == queues);
	verify_chain(copied.native());
	verify_owned_links(copied);

	auto moved = ::std::move(copied);
	assert(moved.native().pNext != full.native().pNext);
	assert(moved.native().pQueueCreateInfos == queues);
	verify_chain(moved.native());
	verify_owned_links(moved);

	auto assigned = full;
	assigned = moved;
	verify_chain(assigned.native());
	verify_owned_links(assigned);
	auto move_assigned = full;
	move_assigned = ::std::move(assigned);
	verify_chain(move_assigned.native());
	verify_owned_links(move_assigned);

	// The move source dies before this object is inspected. Any stale pNext into
	// the source storage is therefore caught here (and by the ASan run).
	auto relocated_after_source_destruction = [&]
	{
		auto source = full;
		return move_out(source);
	}();
	verify_chain(relocated_after_source_destruction.native());
	verify_owned_links(relocated_after_source_destruction);

	// Named evaluated values are referenced; evaluated temporaries are owned.
	auto evaluated_feature2 = evaluate(make_feature2_expression());
	auto borrowed_parent = evaluate(device_parameters | evaluated_feature2);
	assert(borrowed_parent.native().pNext == evaluated_feature2.native_address());
	auto copied_borrowed_parent = borrowed_parent;
	assert(copied_borrowed_parent.native().pNext == evaluated_feature2.native_address());
	auto const const_evaluated_feature2 = evaluate(make_feature2_expression());
	auto const_borrowed_parent = evaluate(
		device_parameters | const_evaluated_feature2);
	assert(const_borrowed_parent.native().pNext ==
		const_evaluated_feature2.native_address());

	auto owned_parent = evaluate(
		device_parameters | evaluate(make_feature2_expression()));
	assert(owned_parent.native().pNext != evaluated_feature2.native_address());
	verify_chain(owned_parent.native());
	verify_owned_links(owned_parent);

	// Both an expression and an already evaluated value call the same create path.
	capture = {};
	VkDevice const first_handle = create_device(
		device_parameters | make_feature2_expression()
			| allocator_param{.callbacks = &allocator},
		device_dispatch{.create = &fake_create_device});
	assert(first_handle == reinterpret_cast<VkDevice>(::std::uintptr_t{0xBADC0DE}));
	assert(capture.physical_device == physical_device);
	assert(capture.allocator == &allocator);
	assert(capture.chain_size == expected_chain().size());

	capture = {};
	VkDevice const second_handle = create_device(
		full, device_dispatch{.create = &fake_create_device});
	assert(second_handle == first_handle);
	assert(capture.chain_size == expected_chain().size());

	capture = {};
	capture.result = VK_ERROR_INITIALIZATION_FAILED;
	bool threw_expected_result = false;
	try
	{
		(void)create_device(
			full, device_dispatch{.create = &fake_create_device});
	}
	catch (VkResult const result)
	{
		threw_expected_result = result == VK_ERROR_INITIALIZATION_FAILED;
	}
	assert(threw_expected_result);
}
