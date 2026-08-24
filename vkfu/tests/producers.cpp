// The generated create_* / allocate_* wrappers: they take an expression where
// Vulkan takes a create-info, and hand the command its native head.
//
// Nothing here calls a driver, so every call goes through the nothrow overload
// against a null device and is expected to fail -- what is under test is that
// the wrappers exist with the right shape and compile.

#include <array>
#include <cassert>
#include <concepts>
#include <expected>
#include <new>
#include <span>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

namespace param = vkfu::param;
using namespace vkfu::enums;

// Shape 1: one structure in, one handle out, with an allocator.
static_assert(::std::same_as<
	decltype(vkfu::create_device(
		VkPhysicalDevice{}, param::device{}, nullptr, ::std::nothrow)),
	::std::expected<VkDevice, VkResult>>);
static_assert(::std::same_as<
	decltype(vkfu::create_device(VkPhysicalDevice{}, param::device{})),
	VkDevice>);

// vkCreateInstance takes no handle at all.
static_assert(::std::same_as<
	decltype(vkfu::create_instance(param::instance{}, nullptr, ::std::nothrow)),
	::std::expected<VkInstance, VkResult>>);

// The vendor tag is a namespace here too.
static_assert(::std::same_as<
	decltype(vkfu::khr::create_swapchain(VkDevice{}, param::khr::swapchain{}, nullptr, ::std::nothrow)),
	::std::expected<VkSwapchainKHR, VkResult>>);

// Shape 2: as many structures as the caller passes, one call.
static_assert(::std::same_as<
	decltype(vkfu::create_graphics_pipelines(
		VkDevice{}, VkPipelineCache{}, nullptr, ::std::nothrow,
		param::graphics_pipeline{}, param::graphics_pipeline{})),
	::std::expected<::std::array<VkPipeline, 2>, VkResult>>);
// ... and the singular convenience.
static_assert(::std::same_as<
	decltype(vkfu::create_graphics_pipeline(VkDevice{}, VkPipelineCache{}, param::graphics_pipeline{})),
	VkPipeline>);

// Shape 3: the count lives in the structure, so the caller sizes the output.
static_assert(::std::same_as<
	decltype(vkfu::allocate_command_buffers(
		VkDevice{}, param::command_buffer{}, ::std::span<VkCommandBuffer>{}, ::std::nothrow)),
	::std::expected<void, VkResult>>);

// A wrapper only accepts an expression that produces its own structure. The
// requirement has to be dependent, or gcc makes the mismatch a hard error.
template<class Expression>
concept buffer_creatable = requires(Expression expression)
{
	vkfu::create_buffer(VkDevice{}, expression, nullptr, ::std::nothrow);
};

static_assert(buffer_creatable<param::buffer>);
static_assert(!buffer_creatable<param::image>);

// A whole chain is an expression too.
template<class Expression>
concept device_creatable = requires(Expression expression)
{
	vkfu::create_device(VkPhysicalDevice{}, expression, nullptr, ::std::nothrow);
};

static_assert(device_creatable<decltype(param::device{} | param::feature::core{})>);

int main()
{
	// A null device makes the driver refuse; the point is that the expression is
	// evaluated, unpacked and handed over without the caller touching a
	// VkStructureType.
	auto outcome = vkfu::create_buffer(
		VkDevice{VK_NULL_HANDLE},
		param::buffer{
			.size = 1024,
			.usage = {.vertex_buffer = 1},
			.sharing_mode = sharing_mode::exclusive,
		},
		nullptr,
		::std::nothrow);
	// Either the loader rejected it or there is no driver at all; both are fine.
	assert(!outcome.has_value() || outcome.value() == VK_NULL_HANDLE);
}
