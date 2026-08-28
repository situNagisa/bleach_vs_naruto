// The extension list a chain needs, derived from the chain.
//
// Forgetting to add "VK_EXT_mesh_shader" next to param::feature::ext::mesh_shader
// is the single most common way to get a create call rejected at runtime. vk.xml
// knows which extension provides which structure, so nobody has to remember.

#include <algorithm>
#include <array>
#include <string_view>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

namespace obj = ::vkfu::obj;
namespace param = ::vkfu::param;

// A structure an extension introduced and core never promoted.
static_assert(::vkfu::vulkan_object_extensions<obj::feature::ext::mesh_shader>::names.size() == 1);
static_assert(::std::string_view{::vkfu::vulkan_object_extensions<obj::feature::ext::mesh_shader>::names[0]}
	== "VK_EXT_mesh_shader");
static_assert(::vkfu::vulkan_object_extensions<obj::feature::ext::mesh_shader>::core == 0);

// A promoted one: core 1.2 has it, and below that the extension does.
static_assert(::vkfu::vulkan_object_extensions<obj::feature::timeline_semaphore>::core == VK_API_VERSION_1_2);
static_assert(::std::string_view{::vkfu::vulkan_object_extensions<obj::feature::timeline_semaphore>::names[0]}
	== "VK_KHR_timeline_semaphore");

// Core since 1.0: nothing to enable, ever.
static_assert(::vkfu::vulkan_object_extensions<obj::buffer>::names.empty());
static_assert(::vkfu::vulkan_object_extensions<obj::buffer>::core == VK_API_VERSION_1_0);

// ------------------------------------------------- derived from a chain

using device_chain = decltype(
	param::device{} | param::feature::core{} | param::feature::ext::mesh_shader{});

// The chain is walked and every object's extensions are collected. On 1.1,
// param::device and feature::core are both core, so only mesh shading is left.
constexpr auto needed = ::vkfu::required_extensions_v<device_chain, VK_API_VERSION_1_1>;
static_assert(needed.size() == 1);
static_assert(::std::string_view{needed[0]} == "VK_EXT_mesh_shader");

// On 1.0 the same chain also needs what promoted VkPhysicalDeviceFeatures2.
constexpr auto on_ten = ::vkfu::required_extensions_v<device_chain, VK_API_VERSION_1_0>;
static_assert(on_ten.size() == 2);
static_assert(::std::string_view{on_ten[0]} == "VK_KHR_get_physical_device_properties2");

// The API version decides whether a promoted object still needs its extension.
using promoted = decltype(param::device{} | param::feature::timeline_semaphore{});
static_assert(::vkfu::required_extensions_v<promoted, VK_API_VERSION_1_2>.empty());
static_assert(::vkfu::required_extensions_v<promoted, VK_API_VERSION_1_0>.size() == 1);

// A structure hanging in a pointer slot is on its own pNext chain, but it still
// has to be enabled. Walking the parent's chain alone would miss it.
using with_slot = decltype(param::graphics_pipeline{
	.color_blend_state = param::state::color_blend{} | param::state::ext::color_write{},
});
constexpr auto through_slot = ::vkfu::required_extensions_v<with_slot, VK_API_VERSION_1_3>;
static_assert(through_slot.size() == 1);
static_assert(::std::string_view{through_slot[0]} == "VK_EXT_color_write_enable");

// Vulkan has two extension lists, and the chain does not decide which one a
// name goes in: VkDeviceGroupDeviceCreateInfo hangs off VkDeviceCreateInfo but
// VK_KHR_device_group_creation is an *instance* extension. So a device chain can
// legitimately need both, and they are asked for separately rather than checked.
using mixed = decltype(
	param::device{} | param::option::device_group{} | param::feature::ext::mesh_shader{});

constexpr auto instance_side =
	::vkfu::required_extensions_v<mixed, VK_API_VERSION_1_0, ::vkfu::extension_scope::instance>;
constexpr auto device_side =
	::vkfu::required_extensions_v<mixed, VK_API_VERSION_1_0, ::vkfu::extension_scope::device>;

static_assert(instance_side.size() == 1);
static_assert(::std::string_view{instance_side[0]} == "VK_KHR_device_group_creation");
static_assert(device_side.size() == 1);
static_assert(::std::string_view{device_side[0]} == "VK_EXT_mesh_shader");
// Together they are exactly what the unsplit answer gives.
static_assert(::vkfu::required_extensions_v<mixed, VK_API_VERSION_1_0>.size() == 2);

int main()
{
	// The deduced form, which is how this is meant to be used at a call site.
	// Not const: evaluate() on a chain is allowed to move out of it.
	auto chain = param::device{} | param::feature::core{} | param::feature::ext::mesh_shader{};
	constexpr auto extensions = ::vkfu::required_extensions<VK_API_VERSION_1_1>(
		param::device{} | param::feature::core{} | param::feature::ext::mesh_shader{});
	static_assert(extensions.size() == 1);

	// The deduced spelling of the same split.
	auto device_chain = param::device{} | param::option::device_group{};
	auto const on_the_instance = ::vkfu::required_instance_extensions<VK_API_VERSION_1_0>(device_chain);
	auto const on_the_device = ::vkfu::required_device_extensions<VK_API_VERSION_1_0>(device_chain);
	static_assert(on_the_instance.size() == 1);
	static_assert(on_the_device.empty());

	auto const listed = ::std::ranges::any_of(extensions, [](char const* entry)
	{
		return ::std::string_view{entry} == "VK_EXT_mesh_shader";
	});
	return (listed && ::vkfu::unpack(::vkfu::evaluate(chain)).sType
		== VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO) ? 0 : 1;
}
