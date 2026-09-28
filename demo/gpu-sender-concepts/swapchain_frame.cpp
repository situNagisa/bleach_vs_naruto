#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>
#include <vkkl/vkkl.h>
#include <vkfu/generated/vulkan-v1.4.328.h>

#include <stdexec/execution.hpp>

#include "./algorithms.h"
#include "./concepts.h"
#include "./vulkan_image_state.h"
#include "./vulkan_tracked_value.h"

/// 场景一：swapchain 一帧的 acquire → clear → present。
///
/// 直接对照 demo/new-arch-pro/entity/render.h 里的 renderer::_begin /
/// renderer::_submit_present_frame：那两个函数手写了同一串 barrier + submit +
/// present，这里用 gpu::vk 这套 CPO/concept/算法把它重新表达成一条 sender 链，
/// 逐行写了原始代码对应到哪一行。这个文件只做真实 Vulkan 类型/API 的编译期验证
/// （clang/gcc + Vulkan-Headers + vkfu，不链接 Vulkan loader），不真正创建窗口、
/// 不真正提交到 GPU——那需要真实设备，这台机器没有。
namespace scene::swapchain_frame
{
/// acquire 的值：真正参与状态转换/同步的部分（image + undefined 状态 + 借来的
/// semaphore）交给 pending_value 表达；image_index 只是渲染逻辑要用的普通数据
/// （present 时选哪个 swapchain 槽位），从头到尾跟着走，不参与任何 transite/
/// consume_external 的类型检查——这正是第二轮讨论里定下的结论：acquire 真实返回值
/// 里唯一有逻辑意义的是 index，其余（fence、semaphore）都被这套机制吸收掉了。
struct acquired
{
	::gpu::vk::pending_value<::VkImage, ::gpu::vk::undefined> pending;
	::std::uint32_t image_index;
};

/// 对应 render.h::_begin 开头的 vkfu::khr::acquire_next_image2 调用，以及它
/// signal 的 image_available、后面在 acquire_barrier 里用到的 undefined 起始
/// 状态。fence 参数在原始代码里也是 VK_NULL_HANDLE——这里不是巧合，是同一个
/// 理由：CPU 不需要在这一步之外单独知道 acquire 完成，函数调用本身同步返回
/// 就是这件事的全部含义。
[[nodiscard]] inline auto acquire(::VkDevice device, ::VkSwapchainKHR swapchain, ::std::span<::VkImage const> images)
{
	return ::stdexec::just() | ::stdexec::then([device, swapchain, images]
	{
		auto semaphore = ::vkkl::semaphore{device, ::vkfu::create_semaphore(device, ::vkfu::param::semaphore{})};
		auto const index = ::vkfu::khr::acquire_next_image2(device, ::vkfu::param::khr::acquire_next_image{
			.swapchain = swapchain,
			.timeout = (::std::numeric_limits<::std::uint64_t>::max)(),
			.semaphore = semaphore.handle,
			.fence = VK_NULL_HANDLE,
			.device_mask = (::std::numeric_limits<::std::uint32_t>::max)(),
		});
		return acquired{
			{images[index], ::gpu::vk::undefined{}, ::std::exchange(semaphore.handle, VK_NULL_HANDLE), device},
			index,
		};
	});
}

/// 对应 render.h::_begin 里 vkCmdClearColorImage 之前那段——原始代码用的是
/// dynamic rendering（vkfu::cmd_begin_rendering/cmd_end_rendering），这里为了
/// 只聚焦"资源状态怎么被 transite 表达"，改成等价但更短的 vkCmdClearColorImage；
/// 两者对这条 sender 链要验证的东西（acquire 借来的 semaphore 被 consume_external
/// 记成 wait、layout 转换被 transite 表达成 barrier）没有区别。
[[nodiscard]] inline auto clear_color(::gpu::vk::domain& dom, ::VkClearColorValue color)
{
	return ::stdexec::then([&dom, color](::gpu::vk::tracked_value<::VkImage, ::gpu::vk::color_attachment> const& item)
	{
		auto const range = ::VkImageSubresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0,
			.levelCount = 1, .baseArrayLayer = 0, .layerCount = 1,
		};
		::vkCmdClearColorImage(dom.command_buffer(), ::gpu::resource(item),
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &color, 1, &range);
		return item;
	});
}

/// 对应 render.h::_submit_present_frame 结尾的 vkfu::khr::queue_present，它要等
/// 的 render_finished 就是 submit(dom) 产出的那个信号——原始代码里 render_finished
/// 是 frame_slot 自己持有的固定 semaphore，这里改成每次 submit 现场借一个，差别
/// 只在生命周期管理（见 vulkan_tracked_value.h::domain::cut 里的说明），不影响
/// 这条链要验证的类型流转。image_index 从 acquired 里带过来，不是猜测或者省略。
[[nodiscard]] inline auto present(::VkQueue present_queue, ::VkSwapchainKHR swapchain, ::std::uint32_t image_index)
{
	return ::stdexec::then([present_queue, swapchain, image_index](
		::gpu::vk::pending_value<::VkImage, ::gpu::vk::present_src> const& p)
	{
		auto const wait = p._signal;
		auto const outcome = ::vkfu::khr::queue_present(present_queue, ::vkfu::param::khr::present{
			.wait_semaphores = ::std::span{&wait, 1u},
			.swapchain_count = 1, .swapchains = &swapchain, .image_indices = &image_index,
		}, ::std::nothrow);
		(void)outcome;
	});
}

/// 完整链：acquire -> consume -> transition(color_attachment) -> clear ->
/// transition(present_src) -> submit -> present。
///
/// 跟 render.h 对照：
///   acquire 的 semaphore = image_available，consume() 消费它，等价于原始代码里
///     acquire_barrier 那次 layout 转换要等的依赖（原始代码没有显式 wait，是因为
///     它把这次转换直接编码成同一命令缓冲内的 barrier；这里因为 acquire 独立成
///     一个 sender 节点，跨越了"提交批次"的边界，所以被 consume_external 记成
///     跨批次的 wait——这正是设计讨论里"跨提交用 semaphore，同提交用 barrier"
///     那条规则的真实体现，不是原始代码和这条链在语义上有差异）。
///   transition(color_attachment) = acquire_barrier（undefined -> color_attachment_optimal）
///   clear_color = vkCmdClearColorImage（原始用 dynamic rendering + secondary
///     command buffer 画三角形，这里简化成 clear，语义上同属"在
///     color_attachment_optimal 状态下写这张图"）
///   transition(present_src) = present_barrier（color_attachment_optimal -> present_src）
///   submit(dom) = vkEndCommandBuffer + vkQueueSubmit2
///   present = vkfu::khr::queue_present
[[nodiscard]] inline auto build_frame(
	::gpu::vk::domain& dom, ::VkDevice device, ::VkSwapchainKHR swapchain,
	::std::span<::VkImage const> images, ::VkQueue present_queue, ::VkClearColorValue color)
{
	return acquire(device, swapchain, images)
		| ::stdexec::let_value([&dom, color, swapchain, present_queue](acquired& a)
		{
			auto const index = a.image_index;
			return ::stdexec::just(::std::move(a.pending))
				| ::gpu::consume()
				| ::gpu::transition(::gpu::vk::color_attachment{})
				| clear_color(dom, color)
				| ::gpu::transition(::gpu::vk::present_src{})
				| ::gpu::submit(dom)
				| present(present_queue, swapchain, index);
		})
		| ::stdexec::write_env(::stdexec::prop{::gpu::vk::get_domain, &dom});
}

// 编译期检查：这条链的类型确实是把 tracked_resource/pending_resource 接到了真实
// VkImage 上，而不是退化成某种类型擦除或者根本没编译到这一步。
static_assert(::gpu::tracked_resource<::gpu::vk::tracked_value<::VkImage, ::gpu::vk::color_attachment>>);
static_assert(::gpu::pending_resource<::gpu::vk::pending_value<::VkImage, ::gpu::vk::present_src>>);
}
