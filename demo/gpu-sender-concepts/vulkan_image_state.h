#pragma once

#include <concepts>
#include <cstdint>

#include <vulkan/vulkan.h>

/// image_state / buffer_state 的真实 Vulkan 实例化：跟 image_state.h（mock 版本）
/// 并列，不共享类型——mock 版本用于 main.cpp 里跟 Vulkan 完全无关的三段验证；这里
/// 用真实的 VkPipelineStageFlags2/VkAccessFlags2/VkImageLayout，供两个真实场景
/// （swapchain_frame.cpp、texture_upload.cpp）编译期验证 CPO/concept/算法能不能
/// 接得上真实的 vkfu/vkkl 类型。concepts.h 里的 tracked_resource/pending_resource
/// 完全不知道这里的存在——它们只要求 resource()/state()/transite() 这三个原语
/// 能调用得通，不关心 State 具体是 mock 的还是真实 Vulkan 的。
namespace gpu::vk
{
template <class S>
concept buffer_state = requires(S const& s)
{
	{ s.stage() } -> ::std::convertible_to<::VkPipelineStageFlags2>;
	{ s.access() } -> ::std::convertible_to<::VkAccessFlags2>;
};

template <class S>
concept image_state = buffer_state<S> && requires(S const& s)
{
	{ s.layout() } -> ::std::convertible_to<::VkImageLayout>;
};

// ---- image：编译期已知的状态是空类型，[[no_unique_address]] 能把它压到零大小 ----

template <::VkPipelineStageFlags2 Stage, ::VkAccessFlags2 Access, ::VkImageLayout Layout>
struct static_image_state
{
	static constexpr ::VkPipelineStageFlags2 stage() noexcept { return Stage; }
	static constexpr ::VkAccessFlags2 access() noexcept { return Access; }
	static constexpr ::VkImageLayout layout() noexcept { return Layout; }

	bool operator==(static_image_state const&) const = default;
};

// ---- image：只能运行时确定的状态（比如从池子里借来的资源，历史状态只有池子知道） ----

struct dynamic_image_state
{
	::VkPipelineStageFlags2 _stage;
	::VkAccessFlags2 _access;
	::VkImageLayout _layout;

	constexpr ::VkPipelineStageFlags2 stage() const noexcept { return _stage; }
	constexpr ::VkAccessFlags2 access() const noexcept { return _access; }
	constexpr ::VkImageLayout layout() const noexcept { return _layout; }

	bool operator==(dynamic_image_state const&) const = default;
};

// render.h::_begin / _submit_present_frame 里手写的两次 layout 转换，搬成两个状态常量。
using undefined = static_image_state<
	::VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, ::VK_ACCESS_2_NONE, ::VK_IMAGE_LAYOUT_UNDEFINED>;
using color_attachment = static_image_state<
	::VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, ::VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
	::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL>;
using present_src = static_image_state<
	::VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, ::VK_ACCESS_2_NONE, ::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR>;

// main_menu.cpp::_upload 里手写的两次 layout 转换。
using transfer_dst = static_image_state<
	::VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, ::VK_ACCESS_2_TRANSFER_WRITE_BIT,
	::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL>;
using shader_read_only = static_image_state<
	::VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, ::VK_ACCESS_2_SHADER_READ_BIT,
	::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>;

static_assert(image_state<undefined> && image_state<color_attachment> && image_state<present_src>);
static_assert(image_state<transfer_dst> && image_state<shader_read_only>);
static_assert(image_state<dynamic_image_state>);
static_assert(::std::is_empty_v<undefined> && ::std::is_empty_v<color_attachment> && ::std::is_empty_v<present_src>);
static_assert(::std::is_empty_v<transfer_dst> && ::std::is_empty_v<shader_read_only>);

// ---- buffer：main_menu.cpp 里 staging buffer 的两个状态 ----

template <::VkPipelineStageFlags2 Stage, ::VkAccessFlags2 Access>
struct static_buffer_state
{
	static constexpr ::VkPipelineStageFlags2 stage() noexcept { return Stage; }
	static constexpr ::VkAccessFlags2 access() noexcept { return Access; }

	bool operator==(static_buffer_state const&) const = default;
};

using buffer_undefined = static_buffer_state<::VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, ::VK_ACCESS_2_NONE>;
using buffer_transfer_src = static_buffer_state<::VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, ::VK_ACCESS_2_TRANSFER_READ_BIT>;

static_assert(buffer_state<buffer_undefined> && buffer_state<buffer_transfer_src>);
static_assert(!image_state<buffer_transfer_src>);   // 缺 layout()
static_assert(buffer_state<color_attachment>);      // image_state 细化自 buffer_state，image 天然也满足它
}
