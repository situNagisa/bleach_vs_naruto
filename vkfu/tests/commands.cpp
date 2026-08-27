// The generated wrappers for everything that is not vkCreate* / vkAllocate*:
// an expression where Vulkan takes a structure, one span where it takes a
// pointer and a count.
//
// These call into the loader, so this is a compile-only check -- building it
// instantiates every template body under test.

#include <array>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <expected>
#include <new>
#include <span>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

namespace param = ::vkfu::param;
using namespace ::vkfu::enums;

// A pointer and its count become one span. This is the shape that used to read
// vkCmdExecuteCommands(cmd, static_cast<uint32_t>(v.size()), v.data()).
static_assert(::std::same_as<
	decltype(vkfu::cmd_execute_commands(VkCommandBuffer{}, ::std::span<VkCommandBuffer const>{})),
	void>);
static_assert(::std::same_as<
	decltype(vkfu::cmd_set_viewport(VkCommandBuffer{}, 0u, ::std::span<VkViewport const>{})),
	void>);
static_assert(::std::same_as<
	decltype(vkfu::cmd_bind_vertex_buffers(
		VkCommandBuffer{}, 0u, ::std::span<VkBuffer const>{}, ::std::span<VkDeviceSize const>{})),
	void>);

// A void command that takes a structure takes an expression instead, and has no
// nothrow twin because there is nothing to fail.
static_assert(::std::same_as<
	decltype(vkfu::cmd_pipeline_barrier2(VkCommandBuffer{}, param::dependency{})),
	void>);
static_assert(::std::same_as<
	decltype(vkfu::cmd_begin_rendering(VkCommandBuffer{}, param::rendering{})),
	void>);

// A VkResult command gets the pair.
static_assert(::std::same_as<
	decltype(vkfu::begin_command_buffer(VkCommandBuffer{}, param::command_buffer_begin{}, ::std::nothrow)),
	::std::expected<void, VkResult>>);
static_assert(::std::same_as<
	decltype(vkfu::begin_command_buffer(VkCommandBuffer{}, param::command_buffer_begin{})),
	void>);
static_assert(::std::same_as<
	decltype(vkfu::queue_submit2(VkQueue{}, ::std::span<VkSubmitInfo2 const>{}, VkFence{}, ::std::nothrow)),
	::std::expected<void, VkResult>>);
static_assert(::std::same_as<
	decltype(vkfu::wait_for_fences(VkDevice{}, ::std::span<VkFence const>{}, VK_TRUE, 0ull, ::std::nothrow)),
	::std::expected<void, VkResult>>);

// The vendor tag is a namespace here too.
static_assert(::std::same_as<
	decltype(vkfu::khr::queue_present(VkQueue{}, param::khr::present{}, ::std::nothrow)),
	::std::expected<void, VkResult>>);

// Several structures in one call, each its own expression.
static_assert(::std::same_as<
	decltype(vkfu::cmd_next_subpass2(
		VkCommandBuffer{}, param::subpass_begin{}, param::subpass_end{})),
	void>);

// A wrapper only accepts an expression that produces its own structure.
template<class Expression>
concept barrier_recordable = requires(Expression expression)
{
	vkfu::cmd_pipeline_barrier2(VkCommandBuffer{}, expression);
};

static_assert(barrier_recordable<param::dependency>);
// param::rendering has pointer slots, so it is a template.
static_assert(!barrier_recordable<param::rendering<>>);

// ------------------------------------------------- structures with no sType

// They get a param too: snake_case fields, scoped enums, bit-field flags. No
// tag, because there is no pNext to chain.
static_assert(::std::same_as<decltype(::vkfu::evaluate(param::viewport{})), VkViewport>);
static_assert(!vkfu::expression<param::viewport>);

constexpr auto full_viewport = ::vkfu::evaluate(param::viewport{
	.width = 1280.0f,
	.height = 720.0f,
	.max_depth = 1.0f,
});
static_assert(full_viewport.width == 1280.0f);
static_assert(full_viewport.maxDepth == 1.0f);

// The ones that used to need hand-written bit ORs. Not constexpr: clang has no
// constexpr bit_cast over bit-fields yet, and flags go through one.
void test_plain_flags()
{
	auto const blend = ::vkfu::evaluate(param::state::color_blend_attachment{
		.color_write_mask = {.r = 1, .g = 1, .b = 1, .a = 1},
	});
	assert(blend.colorWriteMask
		== (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT));
	assert(blend.blendEnable == VK_FALSE);

	auto const binding = ::vkfu::evaluate(param::descriptor_set_layout_binding{
		.binding = 2,
		.descriptor_type = descriptor_type::uniform_buffer,
		.descriptor_count = 1,
		.stage_flags = {.vertex = 1, .fragment = 1},
	});
	assert(binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	assert(binding.stageFlags == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));
}

int main()
{
	// Recording a frame without naming a single VkStructureType.
	auto const command_buffer = VkCommandBuffer{VK_NULL_HANDLE};
	auto const secondaries = ::std::array{VkCommandBuffer{VK_NULL_HANDLE}};

	vkfu::begin_command_buffer(command_buffer, param::command_buffer_begin{
		.flags = {.one_time_submit = 1},
	});

	auto const barrier = ::vkfu::evaluate(param::image_memory_barrier2{
		.dst_stage_mask = {.color_attachment_output = 1},
		.dst_access_mask = {.color_attachment_write = 1},
		.old_layout = image_layout::undefined,
		.new_layout = image_layout::color_attachment_optimal,
		.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
		.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
	});
	vkfu::cmd_pipeline_barrier2(command_buffer, param::dependency{
		.image_memory_barriers = ::std::span{&barrier, 1u},
	});

	auto const viewport = ::vkfu::evaluate(param::viewport{.width = 1280.0f, .height = 720.0f, .max_depth = 1.0f});
	vkfu::cmd_set_viewport(command_buffer, 0, ::std::span{&viewport, 1u});
	vkfu::cmd_execute_commands(command_buffer, secondaries);
	test_plain_flags();
}
