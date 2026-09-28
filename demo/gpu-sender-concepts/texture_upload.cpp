#include <cstdint>
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

/// 场景二：贴图上传——staging buffer 拷进一张 sampled image，供片段着色器读取。
///
/// 直接对照 demo/new-arch-pro/entity/main_menu.cpp 里
/// implementation::_graphics_resources::_upload：那个函数手写了
/// undefined -> transfer_dst -> (copy) -> shader_read_only 这条 layout 转换序列，
/// 这里用同一套 gpu::vk CPO/concept/算法重新表达。跟场景一（swapchain_frame.cpp）
/// 的区别：这条链完全不涉及 swapchain/acquire/present，是纯粹的"资源准备"场景，
/// 用来验证这套设计不是只服务于渲染一帧这一种形状。
namespace scene::texture_upload
{
/// 对应 _upload 里 staging buffer 到 image 之间那次 vkCmdCopyBufferToImage2——
/// 这一步不改变 image 的状态（copy 前后都停在 transfer_dst_optimal），对应我们
/// 设计里"act"这一类操作：resource() 拿到句柄去录命令，返回值类型不变。这里直接
/// 用 stdexec::then 实现，不需要专门的 act 原语（第二轮设计讨论定下的结论：act
/// 就是 then，只要 then 里能拿到 domain 就够了，不需要为它单开一个原语）。
[[nodiscard]] inline auto copy_from_staging(
	::gpu::vk::domain& dom, ::VkBuffer staging, ::VkExtent3D extent)
{
	return ::stdexec::then([&dom, staging, extent](
		::gpu::vk::tracked_value<::VkImage, ::gpu::vk::transfer_dst> const& item)
	{
		auto const region = ::vkfu::evaluate(::vkfu::param::buffer_image_copy2{
			.image_subresource = ::vkfu::evaluate(::vkfu::param::image_subresource_layers{
				.aspect_mask = {.color = 1}, .layer_count = 1,
			}),
			.image_extent = extent,
		});
		::vkfu::cmd_copy_buffer_to_image2(dom.command_buffer(), ::vkfu::param::copy_buffer_to_image2{
			.src_buffer = staging,
			.dst_image = ::gpu::resource(item),
			.dst_image_layout = ::vkfu::enums::image_layout::transfer_dst_optimal,
			.regions = ::std::span{&region, 1u},
		});
		return item;
	});
}

/// 完整链：从 undefined 开始，转到 transfer_dst，拷贝，转到 shader_read_only，
/// 提交、真正等 GPU 做完——对应 _upload 里 vkEndCommandBuffer 之后那几行
/// （queue_submit + wait_for_fences）。跟场景一不同的地方：这里 submit 之后
/// 直接 consume()，且此时不再有 domain 在 env 里（write_env 只包住 submit 之前
/// 的部分）——所以这次 consume_external 走的是"没有 domain，真的去等"那条分支，
/// 对应 _upload 原始代码里 wait_for_fences 那种同步等待。这跟场景一里 consume()
/// 落在 domain 录制链内部（走"记一条 wait"分支）正好是两种不同的消费方式，
/// 呼应了"同一个 pending_value，consume_external 的行为由消费者决定，不是由
/// pending_value 自己决定"这条设计结论。
[[nodiscard]] inline auto build_upload(
	::gpu::vk::domain& dom, ::VkImage image, ::VkBuffer staging, ::VkExtent3D extent)
{
	auto recorded = ::stdexec::just(::gpu::vk::tracked_value<::VkImage, ::gpu::vk::undefined>{image, {}})
		| ::gpu::transition(::gpu::vk::transfer_dst{})
		| copy_from_staging(dom, staging, extent)
		| ::gpu::transition(::gpu::vk::shader_read_only{})
		| ::gpu::submit(dom)
		| ::stdexec::write_env(::stdexec::prop{::gpu::vk::get_domain, &dom});

	// consume() 特意接在 write_env 外面：submit 已经把这条链的"录制中"状态结束了，
	// 这里往后没有 domain 可查，consume_external 会走真正调用 vkfu::wait_semaphores
	// 的那条分支——对应原始代码里 upload 完成后同步等待、staging buffer 才能安全
	// 销毁的那一刻。
	return ::std::move(recorded) | ::gpu::consume();
}

// 编译期检查：跟场景一共用同一套 tracked_value/pending_value 模板，同一份
// transite/consume_external 实现——两个场景（渲染一帧 vs 准备一次性资源）
// 完全不需要各自的一套类型。
static_assert(::gpu::tracked_resource<::gpu::vk::tracked_value<::VkImage, ::gpu::vk::transfer_dst>>);
static_assert(::gpu::pending_resource<::gpu::vk::pending_value<::VkImage, ::gpu::vk::shader_read_only>>);
}
