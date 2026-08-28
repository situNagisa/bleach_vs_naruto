// The read side: two-call enumerates folded into one call, and the commands
// that fill in a pNext chain the caller shapes.
//
// These call into the loader, so this is a compile-only check -- building it
// instantiates every template body under test.

#include <array>
#include <concepts>
#include <expected>
#include <iterator>
#include <new>
#include <span>
// Only for the negative test below: the generated header no longer needs it.
#include <vector>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

namespace obj = ::vkfu::obj;
namespace param = ::vkfu::param;

// ------------------------------------------------- two-call enumerates

// Ask how many, then write where you say. Nothing is allocated, so the header
// needs no container at all.
static_assert(::std::same_as<
	decltype(vkfu::count_physical_devices(VkInstance{})),
	::std::uint32_t>);
static_assert(::std::same_as<
	decltype(vkfu::enumerate_physical_devices(VkInstance{}, ::std::span<VkPhysicalDevice>{})),
	::std::span<VkPhysicalDevice>>);
static_assert(::std::same_as<
	decltype(vkfu::enumerate_physical_devices(VkInstance{}, ::std::span<VkPhysicalDevice>{}, ::std::nothrow)),
	::std::expected<::std::span<VkPhysicalDevice>, VkResult>>);

// std::ranges::copy's shape and its contract: the caller guarantees the room.
// Contiguous rather than merely output, because the driver writes through a
// raw pointer -- a back_insert_iterator has no pointer to give it.
static_assert(::std::same_as<
	decltype(vkfu::enumerate_physical_devices(VkInstance{}, ::std::declval<VkPhysicalDevice*>())),
	VkPhysicalDevice*>);

template<class Out>
concept enumerable_into = requires(Out out) { vkfu::enumerate_physical_devices(VkInstance{}, out); };

static_assert(enumerable_into<VkPhysicalDevice*>);
static_assert(enumerable_into<::std::array<VkPhysicalDevice, 4>::iterator>);
static_assert(!enumerable_into<::std::back_insert_iterator<::std::vector<VkPhysicalDevice>>>);
static_assert(!enumerable_into<VkImage*>);          // wrong element type

// A void enumerate has nothing to fail, so it gets one function and no pair.
static_assert(::std::same_as<
	decltype(vkfu::count_physical_device_queue_family_properties(VkPhysicalDevice{})),
	::std::uint32_t>);
static_assert(::std::same_as<
	decltype(vkfu::get_physical_device_queue_family_properties(
		VkPhysicalDevice{}, ::std::span<VkQueueFamilyProperties>{})),
	::std::span<VkQueueFamilyProperties>>);
// Dependent, because an invalid expression in a non-template `requires` is a
// hard error on g++ rather than an unsatisfied constraint.
template<class Handle>
concept queue_families_can_fail = requires(Handle handle)
{
	vkfu::get_physical_device_queue_family_properties(
		handle, ::std::span<VkQueueFamilyProperties>{}, ::std::nothrow);
};

static_assert(!queue_families_can_fail<VkPhysicalDevice>);

// The vendor tag is a namespace here too.
static_assert(::std::same_as<
	decltype(vkfu::khr::get_swapchain_images(VkDevice{}, VkSwapchainKHR{}, ::std::span<VkImage>{})),
	::std::span<VkImage>>);
static_assert(::std::same_as<
	decltype(vkfu::khr::count_swapchain_images(VkDevice{}, VkSwapchainKHR{})),
	::std::uint32_t>);

// A leading filter parameter stays a parameter.
static_assert(::std::same_as<
	decltype(vkfu::enumerate_device_extension_properties(
		VkPhysicalDevice{}, nullptr, ::std::span<VkExtensionProperties>{})),
	::std::span<VkExtensionProperties>>);

// An opaque blob is bytes.
static_assert(::std::same_as<
	decltype(vkfu::get_pipeline_cache_data(VkDevice{}, VkPipelineCache{}, ::std::span<::std::byte>{})),
	::std::span<::std::byte>>);

// ------------------------------------------------- query chains

// Nothing named: just the head structure, owned and sType-stamped.
static_assert(::std::same_as<
	decltype(vkfu::get_physical_device_properties2(VkPhysicalDevice{})),
	::vkfu::query_chain<obj::property::core>>);

using driver_and_id = decltype(vkfu::get_physical_device_properties2<
	obj::property::driver, obj::property::id>(VkPhysicalDevice{}));
static_assert(::std::same_as<
	driver_and_id,
	::vkfu::query_chain<obj::property::core, obj::property::driver, obj::property::id>>);

// The head reads as the native structure; each extra is reached by its tag.
static_assert(::std::same_as<
	::std::remove_cvref_t<decltype(::std::declval<driver_and_id&>().head())>,
	VkPhysicalDeviceProperties2>);
static_assert(::std::same_as<
	::std::remove_cvref_t<decltype(::std::declval<driver_and_id&>().get<obj::property::driver>())>,
	VkPhysicalDeviceDriverProperties>);

// The same structextends edges that guard the write side guard this one: a
// structure that does not extend VkPhysicalDeviceProperties2 is not accepted.
template<class... Extras>
concept properties_of = requires { vkfu::get_physical_device_properties2<Extras...>(VkPhysicalDevice{}); };

static_assert(properties_of<obj::property::driver>);
static_assert(!properties_of<obj::feature::ext::mesh_shader>);
// Naming the same structure twice would produce a chain that points at itself.
static_assert(!properties_of<obj::property::driver, obj::property::driver>);

// A chain query that also reads an info structure takes it as an expression.
static_assert(::std::same_as<
	decltype(vkfu::get_buffer_memory_requirements2(VkDevice{}, param::buffer_memory_requirements2{})),
	::vkfu::query_chain<obj::result::memory_requirements2>>);

// Old spellings of promoted types resolve to the same structure, one namespace
// deeper. Code written before the promotion keeps compiling.
static_assert(::std::same_as<param::khr::rendering<>, param::rendering<>>);
static_assert(::std::same_as<param::feature::khr::core, param::feature::core>);
static_assert(::std::same_as<obj::feature::khr::timeline_semaphore, obj::feature::timeline_semaphore>);

int main()
{
	// The chain is a value: moving it has to keep pNext pointing at its own
	// members, which is what basic_storage guarantees.
	auto properties = ::vkfu::query_chain<
		obj::property::core, obj::property::driver, obj::property::id>{};
	auto moved = ::std::move(properties);

	auto const& head = moved.head();
	auto const& driver = moved.get<obj::property::driver>();
	auto const& id = moved.get<obj::property::id>();

	if (head.sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
		|| driver.sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES
		|| id.sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES)
	{
		return 1;
	}
	if (head.pNext != static_cast<void const*>(&driver) || driver.pNext != static_cast<void const*>(&id))
	{
		return 2;
	}
	if (id.pNext != nullptr)
	{
		return 3;
	}
	return 0;
}
