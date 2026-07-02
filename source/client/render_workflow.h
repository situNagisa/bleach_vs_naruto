#pragma once

#include <cassert>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <exec/async_scope.hpp>
#include <stdexec/execution.hpp>

#include <bvn/renderer/vulkan_renderer.h>

namespace
{
struct render_workflow
{
	struct state
	{
		using inner_scheduler_type = decltype(::std::declval<::stdexec::run_loop&>().get_scheduler());

		struct parked
		{
			void (*resume)(parked*) noexcept = nullptr;
			parked* next = nullptr;
		};

		::bvn::renderer::vulkan_renderer* renderer = nullptr;
		::stdexec::run_loop _inner_loop{};
		inner_scheduler_type _inner_scheduler = _inner_loop.get_scheduler();
		::exec::async_scope frame_scope{};
		::std::mutex queue_mutex{};
		::std::atomic_bool stopping{false};
		parked* head = nullptr;
		parked* tail = nullptr;
	};

	struct sender;

	struct scheduler
	{
		using scheduler_concept = ::stdexec::scheduler_t;

		::std::shared_ptr<state> _state;

		auto schedule() const noexcept -> sender;

		auto operator==(scheduler const&) const noexcept -> bool = default;
	};

	struct sender
	{
		using sender_concept = ::stdexec::sender_t;
		using completion_signatures = ::stdexec::completion_signatures<::stdexec::set_value_t()>;

		::std::shared_ptr<state> _state;

		template <class receiver_type>
		struct operation : state::parked
		{
			::std::shared_ptr<state> _state;
			receiver_type receiver;

			void start() & noexcept
			{
				this->resume = [](state::parked* node) noexcept
				{
					auto operation = static_cast<sender::operation<receiver_type>*>(node);
					::stdexec::set_value(::std::move(operation->receiver));
				};
				this->next = nullptr;

				auto lock = ::std::lock_guard{_state->queue_mutex};
				if (_state->tail != nullptr)
				{
					_state->tail->next = this;
				}
				else
				{
					_state->head = this;
				}

				_state->tail = this;
			}
		};

		template <class receiver_type>
		auto connect(receiver_type receiver) const -> operation<receiver_type>
		{
			return {{}, _state, ::std::move(receiver)};
		}

		struct env
		{
			::std::shared_ptr<state> _state;

			auto query(::stdexec::get_completion_scheduler_t<::stdexec::set_value_t>) const noexcept -> scheduler
			{
				return {_state};
			}
		};

		auto get_env() const noexcept -> env
		{
			return {_state};
		}
	};

	struct task_queue_sender
	{
		using sender_concept = ::stdexec::sender_t;
		using completion_signatures = ::stdexec::completion_signatures<::stdexec::set_value_t()>;

		state::parked* head = nullptr;

		template <class receiver_type>
		struct operation
		{
			state::parked* head = nullptr;
			receiver_type receiver;

			void start() & noexcept
			{
				auto current = head;
				while (current != nullptr)
				{
					auto next = current->next;
					current->resume(current);
					current = next;
				}

				::stdexec::set_value(::std::move(receiver));
			}
		};

		template <class receiver_type>
		auto connect(receiver_type receiver) const -> operation<receiver_type>
		{
			return {head, ::std::move(receiver)};
		}
	};

	::std::shared_ptr<state> _state = ::std::make_shared<state>();

	auto query(::stdexec::get_scheduler_t) const noexcept -> scheduler
	{
		return {_state};
	}

	void submit() const
	{
		assert(_state != nullptr);
		assert(_state->renderer != nullptr);

		::stdexec::sync_wait(_state->frame_scope.on_empty());

		auto&& r = *_state->renderer;

		//+ begin frame
		{
			auto&& slot = r.slots[r.current_slot];

			if (r.resize_requested.exchange(false, ::std::memory_order_acq_rel))
			{
				if (r.requested_extent.width == 0 || r.requested_extent.height == 0)
				{
					if (_state->stopping.load(::std::memory_order_acquire))
					{
						auto current = static_cast<state::parked*>(nullptr);
						{
							auto lock = ::std::lock_guard{_state->queue_mutex};
							current = _state->head;
							_state->head = nullptr;
							_state->tail = nullptr;
						}

						_state->frame_scope.spawn(::stdexec::starts_on(_state->_inner_scheduler, task_queue_sender{.head = current}));
					}
					return;
				}

				r.resize(r.requested_extent);
			}

			if (::vkWaitForFences(r.device.handle, 1, &slot.in_flight.handle, VK_TRUE, UINT64_MAX) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to wait for in-flight fence"};
			}

			auto acquire_result = ::vkAcquireNextImageKHR(r.device.handle, r.swapchain.handle, UINT64_MAX, slot.image_available.handle, VK_NULL_HANDLE, &r.active_image_index);

			if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				auto extent = r.target_window != nullptr ? r.target_window->drawable_extent() : ::bvn::platform::window_extent{r.swapchain_extent.width, r.swapchain_extent.height};
				if (extent.width != 0 && extent.height != 0)
				{
					r.resize(extent);
				}
				else if (_state->stopping.load(::std::memory_order_acquire))
				{
					auto current = static_cast<state::parked*>(nullptr);
					{
						auto lock = ::std::lock_guard{_state->queue_mutex};
						current = _state->head;
						_state->head = nullptr;
						_state->tail = nullptr;
					}

					_state->frame_scope.spawn(::stdexec::starts_on(_state->_inner_scheduler, task_queue_sender{.head = current}));
				}
				return;
			}

			if (acquire_result == VK_SUBOPTIMAL_KHR)
			{
				r.requested_extent = r.target_window != nullptr ? r.target_window->drawable_extent() : ::bvn::platform::window_extent{r.swapchain_extent.width, r.swapchain_extent.height};
				r.resize_requested.store(true, ::std::memory_order_release);
			}

			if (acquire_result != ::VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR)
			{
				throw ::std::runtime_error{"failed to acquire swapchain image"};
			}

			if (::vkResetFences(r.device.handle, 1, &slot.in_flight.handle) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to reset in-flight fence"};
			}

			if (::vkResetCommandPool(r.device.handle, slot.command_pool.handle, 0) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to reset frame command pool"};
			}

			auto begin_info = VkCommandBufferBeginInfo{};
			begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			if (::vkBeginCommandBuffer(slot.command_buffer, &begin_info) != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"failed to begin frame command buffer"};
			}

			r.command_buffer = slot.command_buffer;
		}

		//+ begin rendering
		{
			auto&& slot = r.slots[r.current_slot];
			auto command = slot.command_buffer;

			//+ transition swapchain image into color attachment layout
			{
				auto barrier = VkImageMemoryBarrier2{};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
				barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
				barrier.srcAccessMask = 0;
				barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
				barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = r.swapchain_images[r.active_image_index];
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = 1;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = 1;

				auto dependency = VkDependencyInfo{};
				dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
				dependency.imageMemoryBarrierCount = 1;
				dependency.pImageMemoryBarriers = &barrier;
				::vkCmdPipelineBarrier2(command, &dependency);
			}

			//+ transition this frame slot's depth image into depth attachment layout
			{
				auto barrier = VkImageMemoryBarrier2{};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
				barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
				barrier.srcAccessMask = 0;
				barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
				barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = slot.depth_image.image.handle;
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = 1;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = 1;

				auto dependency = VkDependencyInfo{};
				dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
				dependency.imageMemoryBarrierCount = 1;
				dependency.pImageMemoryBarriers = &barrier;
				::vkCmdPipelineBarrier2(command, &dependency);
			}

			auto clear_color = VkClearValue{};
			clear_color.color = {{0.03f, 0.035f, 0.045f, 1.0f}};

			auto clear_depth = VkClearValue{};
			clear_depth.depthStencil = {1.0f, 0};

			auto color_attachment = VkRenderingAttachmentInfo{};
			color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			color_attachment.imageView = r.swapchain_image_views[r.active_image_index].handle;
			color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			color_attachment.clearValue = clear_color;

			auto depth_attachment = VkRenderingAttachmentInfo{};
			depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			depth_attachment.imageView = slot.depth_image.view.handle;
			depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depth_attachment.clearValue = clear_depth;

			auto render_area = VkRect2D{};
			render_area.offset = {0, 0};
			render_area.extent = r.swapchain_extent;

			auto rendering_info = VkRenderingInfo{};
			rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			rendering_info.renderArea = render_area;
			rendering_info.layerCount = 1;
			rendering_info.colorAttachmentCount = 1;
			rendering_info.pColorAttachments = &color_attachment;
			rendering_info.pDepthAttachment = &depth_attachment;

			::vkCmdBeginRendering(command, &rendering_info);
		}

		auto current = static_cast<state::parked*>(nullptr);
		{
			auto lock = ::std::lock_guard{_state->queue_mutex};
			current = _state->head;
			_state->head = nullptr;
			_state->tail = nullptr;
		}

		auto work =
			::stdexec::when_all(task_queue_sender{.head = current})
			| ::stdexec::then([state = _state]
			{
				auto&& r = *state->renderer;

				//+ end rendering
				{
					::vkCmdEndRendering(r.slots[r.current_slot].command_buffer);
				}

				//+ end frame
				{
					auto&& slot = r.slots[r.current_slot];

					//+ transition swapchain image to present layout
					{
						auto barrier = VkImageMemoryBarrier2{};
						barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
						barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
						barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
						barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
						barrier.dstAccessMask = 0;
						barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
						barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						barrier.image = r.swapchain_images[r.active_image_index];
						barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						barrier.subresourceRange.baseMipLevel = 0;
						barrier.subresourceRange.levelCount = 1;
						barrier.subresourceRange.baseArrayLayer = 0;
						barrier.subresourceRange.layerCount = 1;

						auto dependency = VkDependencyInfo{};
						dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
						dependency.imageMemoryBarrierCount = 1;
						dependency.pImageMemoryBarriers = &barrier;
						::vkCmdPipelineBarrier2(slot.command_buffer, &dependency);
					}

					if (::vkEndCommandBuffer(slot.command_buffer) != ::VK_SUCCESS)
					{
						throw ::std::runtime_error{"failed to end frame command buffer"};
					}

					auto signal_semaphore = r.render_finished[r.active_image_index].handle;

					auto wait_info = VkSemaphoreSubmitInfo{};
					wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
					wait_info.semaphore = slot.image_available.handle;
					wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

					auto command_info = VkCommandBufferSubmitInfo{};
					command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
					command_info.commandBuffer = slot.command_buffer;

					auto signal_info = VkSemaphoreSubmitInfo{};
					signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
					signal_info.semaphore = signal_semaphore;
					signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

					auto submit_info = VkSubmitInfo2{};
					submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
					submit_info.waitSemaphoreInfoCount = 1;
					submit_info.pWaitSemaphoreInfos = &wait_info;
					submit_info.commandBufferInfoCount = 1;
					submit_info.pCommandBufferInfos = &command_info;
					submit_info.signalSemaphoreInfoCount = 1;
					submit_info.pSignalSemaphoreInfos = &signal_info;

					if (::vkQueueSubmit2(r.graphics_queue, 1, &submit_info, slot.in_flight.handle) != ::VK_SUCCESS)
					{
						throw ::std::runtime_error{"failed to submit graphics queue"};
					}

					auto present_info = VkPresentInfoKHR{};
					present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
					present_info.waitSemaphoreCount = 1;
					present_info.pWaitSemaphores = &signal_semaphore;
					present_info.swapchainCount = 1;
					auto swapchain = r.swapchain.handle;
					present_info.pSwapchains = &swapchain;
					present_info.pImageIndices = &r.active_image_index;

					auto present_result = ::vkQueuePresentKHR(r.present_queue, &present_info);

					r.command_buffer = VK_NULL_HANDLE;
					r.current_slot = (r.current_slot + 1) % ::bvn::renderer::frames_in_flight;

					if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
					{
						auto extent = r.target_window != nullptr ? r.target_window->drawable_extent() : ::bvn::platform::window_extent{r.swapchain_extent.width, r.swapchain_extent.height};
						if (extent.width != 0 && extent.height != 0)
						{
							r.resize(extent);
							r.resize_requested.store(false, ::std::memory_order_release);
						}
					}
					else if (present_result != ::VK_SUCCESS)
					{
						throw ::std::runtime_error{"failed to present swapchain image"};
					}
				}
			});

		_state->frame_scope.spawn(::stdexec::starts_on(_state->_inner_scheduler, ::std::move(work)));
	}
};

inline auto render_workflow::scheduler::schedule() const noexcept -> sender
{
	return {_state};
}

static_assert(::stdexec::scheduler<render_workflow::scheduler>);
static_assert(::stdexec::scheduler<decltype(::stdexec::get_scheduler(::std::declval<render_workflow const&>()))>);
}
