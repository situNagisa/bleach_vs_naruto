#pragma once

#include <bit>
#include <cassert>
#include <span>
#include <stdexcept>
#include <vector>

#include <vulkan/vulkan.h>
#include <vkkl/vkkl.h>
#include <vkfu/generated/vulkan-v1.4.328.h>

#include <stdexec/execution.hpp>

#include "./concepts.h"
#include "./vulkan_image_state.h"

/// domain、pending_value、tracked_value 的真实 Vulkan 实现：跟 tracked_value.h
/// （mock 版本）并列，验证同一套 transite/consume_external 原语接上真实的
/// vkfu 调用（vkCmdPipelineBarrier2、vkQueueSubmit2、vkWaitSemaphores）是否成立。
///
/// 这里的 domain 只管"往哪个命令缓冲录、结束时提交给哪个队列"，跟 tracked_value.h
/// 里那份一样是最简单的占位实现——没有 reactor 线程、没有 timeline，真正的异步
/// GPU 完成通知属于另一层设计（demo/gpu-sender 已经原型过），这里只验证
/// CPO/concept/算法本身能不能对接真实类型和真实调用。consume_external 走
/// vkWaitSemaphores 而不是 timeline，效果上等价于 render.h 里
/// _wait_fence 那种同步等待，只是这里用 binary semaphore 而不是 fence
/// ——真实实现应当换成 timeline，这不是这一层要解决的问题。
namespace gpu::vk
{
inline void check(::VkResult result, char const* what)
{
	if (result != VK_SUCCESS)
		throw ::std::runtime_error{what};
}

/// domain::cut() 提交这一批之后产出的东西：一个 GPU 侧的信号，和借它的 device——
/// pending_value 需要 device 才能在 consume_external 走到"没有 domain"那条分支时
/// 真的调用 vkfu::wait_semaphores。跟 mock 版本的 domain::cut() 只返回一个裸
/// signal_handle 相比，这里多带了 device，因为真实的等待调用需要它，mock 的
/// wait_signal 不需要。
struct submitted
{
	::VkSemaphore signal;
	::VkDevice device;
};

class domain
{
public:
	domain(::VkDevice device, ::VkQueue queue, ::VkCommandPool pool, ::VkFence fence)
		: _device(device), _queue(queue), _pool(pool), _fence(fence)
	{
		_allocate_and_begin();
	}

	[[nodiscard]] ::VkCommandBuffer command_buffer() const noexcept { return _command; }

	void add_wait(::VkSemaphore semaphore, ::VkPipelineStageFlags2 stage)
	{
		_waits.push_back(::vkfu::evaluate(::vkfu::param::semaphore_submit{
			.semaphore = semaphore,
			.stage_mask = ::std::bit_cast<::vkfu::param::semaphore_submit::stage_mask_type>(stage),
		}));
	}

	/// 结束当前命令缓冲、提交、拿到这批 signal 出来的 semaphore，重开一个命令缓冲。
	/// 跟 render.h::_submit_present_frame 对应的那几步（vkEndCommandBuffer、
	/// 组装 VkSubmitInfo2、vkQueueSubmit2）完全一致，只是这里额外借了一个
	/// binary semaphore 供下游 consume_external 去等，render.h 里那处直接
	/// 复用了固定的 render_finished，这里为了可重入多次调用而每次新建。
	[[nodiscard]] submitted cut()
	{
		check(::vkEndCommandBuffer(_command), "domain: end command buffer");

		auto signal_semaphore = ::vkkl::semaphore{_device, ::vkfu::create_semaphore(_device, ::vkfu::param::semaphore{})};
		auto const command_info = ::vkfu::evaluate(::vkfu::param::command_buffer_submit{.command_buffer = _command});
		auto const signal_info = ::vkfu::evaluate(::vkfu::param::semaphore_submit{
			.semaphore = signal_semaphore.handle, .stage_mask = {.all_commands = 1},
		});
		auto const submit = ::vkfu::evaluate(::vkfu::param::submit2{
			.wait_semaphore_infos = _waits,
			.command_buffer_infos = ::std::span{&command_info, 1u},
			.signal_semaphore_infos = ::std::span{&signal_info, 1u},
		});
		::vkfu::queue_submit2(_queue, ::std::span{&submit, 1u}, _fence);
		_waits.clear();

		_allocate_and_begin();
		// pending_value 从这里开始接手这个 semaphore 的生命周期（要么被
		// consume_external 转发给 vkfu::wait_semaphores 后销毁，要么被
		// add_wait 记进下一批的 wait 列表、由那一批的 domain 再次转手）——
		// vkkl::semaphore 没有 release()，直接借出裸 handle，用 std::exchange
		// 让这个局部 RAII 对象不在这里重复销毁它。
		return {::std::exchange(signal_semaphore.handle, VK_NULL_HANDLE), _device};
	}

private:
	void _allocate_and_begin()
	{
		check(::vkResetCommandPool(_device, _pool, 0), "domain: reset command pool");
		::vkfu::allocate_command_buffers(_device, ::vkfu::param::command_buffer{
			.command_pool = _pool, .level = ::vkfu::enums::command_buffer_level::primary, .command_buffer_count = 1,
		}, ::std::span{&_command, 1u});
		::vkfu::begin_command_buffer(_command, ::vkfu::param::command_buffer_begin{.flags = {.one_time_submit = 1}});
	}

	::VkDevice _device;
	::VkQueue _queue;
	::VkCommandPool _pool;
	::VkFence _fence;
	::VkCommandBuffer _command = VK_NULL_HANDLE;
	::std::vector<::VkSemaphoreSubmitInfo> _waits{};
};

/// get_domain：跟 tracked_value.h 里那份形状完全一样，只是查的是真实 Vulkan 的
/// domain。两份 get_domain_t 分别属于 gpu（mock）和 gpu::vk（真实）两个命名空间，
/// 互不冲突——真实场景的 sender 链只应该看到这一份。
struct get_domain_t : ::stdexec::forwarding_query_t
{
	template <class Env>
	domain* operator()(Env const& env) const noexcept
	{
		if constexpr (requires { { env.query(get_domain_t{}) } -> ::std::convertible_to<domain*>; })
			return env.query(*this);
		else
			return nullptr;
	}
};
inline constexpr get_domain_t get_domain{};

template <class Handle, class State>
struct tracked_value;

template <class Handle, class State>
struct consume_external_sender
{
	using sender_concept = ::stdexec::sender_t;
	using completion_signatures = ::stdexec::completion_signatures<::stdexec::set_value_t(tracked_value<Handle, State>)>;

	Handle _handle;
	State _state;
	::VkSemaphore _signal;
	::VkDevice _device;

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		Handle _handle;
		State _state;
		::VkSemaphore _signal;
		::VkDevice _device;
		Receiver _receiver;

		void start() & noexcept
		{
			if (auto* dom = ::gpu::vk::get_domain(::stdexec::get_env(_receiver)))
			{
				// 要等到哪个 stage，就是 State 本身要求的那个 stage——resource
				// 接下来要被用在 State 描述的那种访问上，这正是 render.h 里
				// acquire_barrier 手写 `dst_stage_mask = color_attachment_output`
				// 那个值的来源：下一步要往 color attachment 写，所以等到这个
				// stage 就够了。不需要另外一个字段去存"该等哪个 stage"。
				dom->add_wait(_signal, _state.stage());
			}
			else
			{
				// 没有 domain 在录制：这是纯 CPU 观察点，真的去等——对应
				// render.h::_wait_fence 那种同步等待，只是这里等的是 binary
				// semaphore（vkWaitSemaphores 要求 timeline semaphore；真实实现
				// 应换成 timeline，这里只是把"consume_external 在没有 domain 时
				// 真的等待"这条路径接到一个能编译、能说明语义的真实调用上）。
				auto const wait = ::vkfu::evaluate(::vkfu::param::semaphore_wait{
					.semaphore_count = 1, .semaphores = &_signal,
				});
				(void)::vkfu::wait_semaphores(_device, wait, (::std::numeric_limits<::std::uint64_t>::max)(), ::std::nothrow);
			}
			::stdexec::set_value(::std::move(_receiver), tracked_value<Handle, State>{_handle, _state});
		}
	};

	template <class Receiver>
	[[nodiscard]] operation<Receiver> connect(Receiver receiver) const
	{
		return {_handle, _state, _signal, _device, ::std::move(receiver)};
	}
};

/// pending_value：句柄 + 状态 + 这批提交出来的 semaphore + 借这个 semaphore 用的
/// device（vkfu::wait_semaphores 需要它，consume_external 走到"没有 domain"那条
/// 分支时才用得上）。要等哪个 stage 直接问 State（见 consume_external_sender 里
/// 的说明），不需要单独存。
template <class Handle, class State>
struct pending_value
{
	Handle _handle;
	State _state;
	::VkSemaphore _signal;
	::VkDevice _device;

	[[nodiscard]] consume_external_sender<Handle, State> consume_external() const
	{
		return {_handle, _state, _signal, _device};
	}
};

/// make_pending 的 ADL 实现：algorithms.h::submit 靠这个把"提交完的 tracked_resource
/// + 这批 cut() 出来的信息"包成 gpu::vk 命名空间的 pending_value，submit 自己不
/// 需要认识这个类型模板。submitted（signal + device）的定义在文件靠前的位置，
/// domain::cut() 也用它作返回类型。
template <class Handle, class State>
[[nodiscard]] pending_value<Handle, State> make_pending(tracked_value<Handle, State> const& item, submitted s)
{
	return {::gpu::resource(item), ::gpu::state(item), s.signal, s.device};
}

/// transite 的真实实现：按 From/To 是不是 image_state/buffer_state 分派，
/// 对应 render.h/main_menu.cpp 里手写的 vkfu::param::image_memory_barrier2 /
/// buffer_memory_barrier2 + cmd_pipeline_barrier2 那几行。
template <class Handle, class From, class To>
struct transite_sender
{
	using sender_concept = ::stdexec::sender_t;
	using completion_signatures = ::stdexec::completion_signatures<::stdexec::set_value_t(tracked_value<Handle, To>)>;

	Handle _handle;
	From _from;
	To _to;

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		Handle _handle;
		From _from;
		To _to;
		Receiver _receiver;

		void start() & noexcept
		{
			auto* dom = ::gpu::vk::get_domain(::stdexec::get_env(_receiver));
			assert(dom != nullptr && "transite: 只能在还有 domain 在录制的链路里调用");
			namespace param = ::vkfu::param;
			if constexpr (image_state<From> && image_state<To>)
			{
				auto const barrier = ::vkfu::evaluate(param::image_memory_barrier2{
					.src_stage_mask = ::std::bit_cast<param::image_memory_barrier2::src_stage_mask_type>(_from.stage()),
					.src_access_mask = ::std::bit_cast<param::image_memory_barrier2::src_access_mask_type>(_from.access()),
					.dst_stage_mask = ::std::bit_cast<param::image_memory_barrier2::dst_stage_mask_type>(_to.stage()),
					.dst_access_mask = ::std::bit_cast<param::image_memory_barrier2::dst_access_mask_type>(_to.access()),
					.old_layout = static_cast<::vkfu::enums::image_layout>(_from.layout()),
					.new_layout = static_cast<::vkfu::enums::image_layout>(_to.layout()),
					.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
					.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
					.image = _handle,
					.subresource_range = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0,
						.levelCount = 1, .baseArrayLayer = 0, .layerCount = 1,
					},
				});
				::vkfu::cmd_pipeline_barrier2(dom->command_buffer(), param::dependency{
					.image_memory_barriers = ::std::span{&barrier, 1u},
				});
			}
			else if constexpr (buffer_state<From> && buffer_state<To>)
			{
				auto const barrier = ::vkfu::evaluate(param::buffer_memory_barrier2{
					.src_stage_mask = ::std::bit_cast<param::buffer_memory_barrier2::src_stage_mask_type>(_from.stage()),
					.src_access_mask = ::std::bit_cast<param::buffer_memory_barrier2::src_access_mask_type>(_from.access()),
					.dst_stage_mask = ::std::bit_cast<param::buffer_memory_barrier2::dst_stage_mask_type>(_to.stage()),
					.dst_access_mask = ::std::bit_cast<param::buffer_memory_barrier2::dst_access_mask_type>(_to.access()),
					.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
					.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
					.buffer = _handle,
					.offset = 0,
					.size = VK_WHOLE_SIZE,
				});
				::vkfu::cmd_pipeline_barrier2(dom->command_buffer(), param::dependency{
					.buffer_memory_barriers = ::std::span{&barrier, 1u},
				});
			}
			else
			{
				static_assert(::gpu::detail::always_false<To>, "transite: From/To 必须同属 image_state 或同属 buffer_state");
			}
			::stdexec::set_value(::std::move(_receiver), tracked_value<Handle, To>{_handle, _to});
		}
	};

	template <class Receiver>
	[[nodiscard]] operation<Receiver> connect(Receiver receiver) const
	{
		return {_handle, _from, _to, ::std::move(receiver)};
	}
};

template <class Handle, class State>
struct tracked_value
{
	Handle _handle;
	State _state;

	[[nodiscard]] Handle resource() const noexcept { return _handle; }
	[[nodiscard]] State state() const noexcept { return _state; }

	template <class To>
	[[nodiscard]] transite_sender<Handle, State, To> transite(To to) const
	{
		return {_handle, _state, ::std::move(to)};
	}
};

static_assert(::gpu::tracked_resource<tracked_value<::VkImage, undefined>>);
static_assert(::gpu::pending_resource<pending_value<::VkImage, undefined>>);
}
