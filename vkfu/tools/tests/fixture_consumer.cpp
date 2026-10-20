// Proves the fixture path produces real C++, not just plausible text: every
// construct the generator has a branch for is built, evaluated and inspected.
//
// The declarations below stand in for a driver header the fixture registry
// describes but that does not exist. Built by tests/run.sh.
#include <cstdint>
#include <ranges>
#include <vulkan/vulkan.h>

// The fixture invents these; vulkan.h does not have them.
using VkWidget = struct VkWidget_T*;
using VkRecorder = struct VkRecorder_T*;
using VkWidgetFlags = ::std::uint32_t;
enum VkWidgetMode : ::std::uint32_t { VK_WIDGET_MODE_PLAIN = 0, VK_WIDGET_MODE_FANCY = 1 };
enum VkWidgetFlagBits : ::std::uint32_t {
	VK_WIDGET_NONE = 0, VK_WIDGET_ALPHA_BIT = 1u << 0, VK_WIDGET_GAMMA_BIT = 1u << 2,
};
constexpr auto VK_STRUCTURE_TYPE_WIDGET_CREATE_INFO = static_cast<VkStructureType>(1000900000);
constexpr auto VK_STRUCTURE_TYPE_WIDGET_PART = static_cast<VkStructureType>(1000900001);
constexpr auto VK_STRUCTURE_TYPE_WIDGET_TUNING = static_cast<VkStructureType>(1000900002);
constexpr auto VK_STRUCTURE_TYPE_WIDGET_COLOUR_INFO_EXT = static_cast<VkStructureType>(1000900003);
constexpr auto VK_STRUCTURE_TYPE_WIDGET_LABEL_INFO_KHR = static_cast<VkStructureType>(1000900004);
constexpr auto VK_STRUCTURE_TYPE_WIDGET_LIMITS = static_cast<VkStructureType>(1000900005);
constexpr auto VK_STRUCTURE_TYPE_WIDGET_PROPERTIES = static_cast<VkStructureType>(1000900006);
constexpr auto VK_STRUCTURE_TYPE_WIDGET_DRIVER_PROPERTIES_EXT = static_cast<VkStructureType>(1000900007);
struct VkExtent { ::std::uint32_t width; ::std::uint32_t height; };
struct VkZone { VkWidgetMode mode; ::std::uint32_t depth; };
struct VkBounds { VkZone zone; VkExtent size; };
struct VkWidgetPart { VkStructureType sType; void const* pNext; ::std::uint32_t index; };
struct VkWidgetTuning { VkStructureType sType; void const* pNext; float bias; };
struct VkWidgetCreateInfo {
	VkStructureType sType; void const* pNext; VkWidgetFlags flags; VkWidgetMode mode;
	VkBool32 enableTracing; ::std::uint32_t partCount; VkWidgetPart const* pParts;
	VkWidgetTuning const* pTuning; VkExtent extent; VkBounds bounds; float weights[4];
};
struct VkWidgetColourInfoEXT { VkStructureType sType; void const* pNext; VkWidgetMode fallbackMode; };
struct VkWidgetLabelInfoKHR { VkStructureType sType; void const* pNext; ::std::uint32_t tag; };
struct VkWidgetLimits { VkStructureType sType; void const* pNext; ::std::uint32_t maxParts; };
struct VkWidgetProperties { VkStructureType sType; void* pNext; ::std::uint32_t revision; };
struct VkWidgetDriverPropertiesEXT { VkStructureType sType; void* pNext; ::std::uint32_t driverVersion; };
struct VkWidgetReport { ::std::uint32_t code; };
extern "C" {
VkResult vkCreateWidget(VkDevice, VkWidgetCreateInfo const*, VkAllocationCallbacks const*, VkWidget*);
void vkCmdSubmitParts(VkRecorder, ::std::uint32_t, VkWidgetPart const*);
void vkCmdEndWidget(VkRecorder);
VkResult vkEnumerateWidgetReports(VkDevice, ::std::uint32_t*, VkWidgetReport*);
void vkGetWidgetProperties(VkDevice, VkWidgetProperties*);
}

#include "golden/vulkan/v1.1.42.h"

namespace param = ::vkfu::param;
namespace tag = ::vkfu::tag;

int main()
{
	auto const parts = ::std::array{VkWidgetPart{VK_STRUCTURE_TYPE_WIDGET_PART, nullptr, 7}};
	auto expression = param::widget{
		.flags = {.alpha = 1, .gamma = 1},
		.mode = ::vkfu::enums::widget_mode::fancy,
		.enable_tracing = true,
		.parts = parts,
		// VkExtent is the native structure under another name, so the field keeps it
		// and a VkExtent that came from Vulkan goes straight in.
		.extent = VkExtent{.width = 4, .height = 2},
		// VkZone has an enum, so a field of that type takes its param, nested ones included.
		.bounds = {.zone = {.mode = ::vkfu::enums::widget_mode::fancy, .depth = 3}, .size = {.width = 8, .height = 6}},
		.weights = {1.0f, 2.0f, 3.0f, 4.0f},
	} | param::option::ext::widget_colour{.fallback_mode = ::vkfu::enums::widget_mode::plain};

	auto storage = ::vkfu::evaluate(expression);
	auto const& head = ::vkfu::unpack(storage);
	if (head.flags != (VK_WIDGET_ALPHA_BIT | VK_WIDGET_GAMMA_BIT)) return 1;
	if (head.weights[3] != 4.0f) return 2;          // the slot+array path
	if (head.partCount != 1 || head.pParts != parts.data()) return 3;
	if (head.extent.width != 4) return 4;
	if (head.bounds.size.height != 6 || head.bounds.zone.depth != 3 || head.bounds.zone.mode != VK_WIDGET_MODE_FANCY) return 7;
	auto const* next = static_cast<VkWidgetColourInfoEXT const*>(head.pNext);
	if (next == nullptr || next->sType != VK_STRUCTURE_TYPE_WIDGET_COLOUR_INFO_EXT) return 5;

	// The query chain, and the extension list derived from the chain.
	auto chain = ::vkfu::query_chain<tag::result::widget_properties, tag::result::ext::widget_driver_properties>{};
	if (chain.head().pNext != static_cast<void const*>(&chain.get<tag::result::ext::widget_driver_properties>())) return 6;
	static_assert(::vkfu::required_extensions_v<decltype(param::widget{} | param::option::widget_limits{}), VK_API_VERSION_1_0>.size() == 1);
	static_assert(::vkfu::required_extensions_v<decltype(param::widget{} | param::option::widget_limits{}), VK_API_VERSION_1_1>.empty());
	return 0;
}
