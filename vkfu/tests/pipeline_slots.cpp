// A pointer member that names another structure is a slot: it takes a whole
// expression, and the parent's storage owns the evaluated child.

#include <cassert>
#include <concepts>
#include <span>
#include <tuple>
#include <utility>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

namespace obj = vkfu::obj;
namespace param = vkfu::param;

// An unfilled slot leaves the native pointer null.
static_assert(vkfu::expression<param::graphics_pipeline<>>);

// A slot only accepts an expression that produces the structure it points at.
template<class... Expressions>
concept pipeable = requires(Expressions... expressions) { (... | expressions); };

static_assert(pipeable<param::state::vertex_input, param::state::vertex_input_divisor>);
static_assert(!pipeable<param::state::vertex_input, param::state::viewport>);

template<class Storage>
void check_wiring(Storage& storage)
{
	auto&& native = storage.native;
	assert(native.sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
	// Filled slots point into this storage; unfilled ones stay null.
	assert(native.pVertexInputState != nullptr);
	assert(native.pInputAssemblyState != nullptr);
	assert(native.pRasterizationState != nullptr);
	assert(native.pTessellationState == nullptr);
	assert(native.pViewportState == nullptr);
	assert(native.pMultisampleState == nullptr);
	assert(native.pDepthStencilState == nullptr);
	assert(native.pColorBlendState == nullptr);
	assert(native.pDynamicState == nullptr);

	assert(native.pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	assert(native.pInputAssemblyState->primitiveRestartEnable == VK_TRUE);
	assert(native.pRasterizationState->lineWidth == 2.0f);
	assert(native.pRasterizationState->cullMode == VK_CULL_MODE_BACK_BIT);

	// The vertex input slot holds a two-node pNext chain of its own.
	auto&& vertex_input = ::std::get<0>(storage.slots).storage;
	auto&& divisor = ::std::get<1>(vertex_input.storages);
	assert(native.pVertexInputState == vkfu::address(vertex_input));
	assert(native.pVertexInputState->pNext == vkfu::address(divisor));
	assert(divisor.vertexBindingDivisorCount == 1);
	assert(divisor.pNext == nullptr);
}

int main()
{
	auto binding = VkVertexInputBindingDescription{.binding = 0, .stride = 12, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
	auto divisor = VkVertexInputBindingDivisorDescription{.binding = 0, .divisor = 1};

	auto pipeline = param::graphics_pipeline{
		.vertex_input_state = param::state::vertex_input{
			.vertex_binding_descriptions = ::std::span{&binding, 1u},
		} | param::state::vertex_input_divisor{
			.vertex_binding_divisors = ::std::span{&divisor, 1u},
		},
		.input_assembly_state = param::state::input_assembly{
			.topology = vkfu::enums::primitive_topology::triangle_list,
			.primitive_restart_enable = true,
		},
		.rasterization_state = param::state::rasterization{
			.cull_mode = {.back = 1},
			.line_width = 2.0f,
		},
	};

	auto storage = vkfu::evaluate(pipeline);
	check_wiring(storage);

	// Copying must re-point the parent at the copy's own children.
	auto copied = storage;
	check_wiring(copied);
	assert(copied.native.pVertexInputState != storage.native.pVertexInputState);

	auto moved = ::std::move(copied);
	check_wiring(moved);
	assert(moved.native.pVertexInputState != storage.native.pVertexInputState);
}
