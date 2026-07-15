#include <stdexcept>
#include <utility>
#include <vector>

#include <bvn/graphics/render_workflow.h>
#include <bvn/graphics/environment.h>

NAGISA_BUILD_LIB_DETAIL_BEGIN

auto render_frame_state::publish(::std::size_t index, render_frame_completion_callback& callback) noexcept
	-> bool
{
	assert(callback._object != nullptr);
	assert(callback._complete != nullptr);
	auto lock = ::std::scoped_lock{_mutex};
	if (_completed)
	{
		return false;
	}

	assert(index < _secondary_commands.size());
	auto&& command = _secondary_commands[index];
	assert(!command._published);
	command._published = true;
	callback._next = ::std::exchange(_callbacks, &callback);
	return true;
}

auto render_frame_state::complete() noexcept -> void
{
	auto callbacks = static_cast<render_frame_completion_callback*>(nullptr);
	{
		auto lock = ::std::scoped_lock{_mutex};
		if (_completed)
		{
			return;
		}

		_completed = true;
		callbacks = ::std::exchange(_callbacks, nullptr);
	}

	while (callbacks != nullptr)
	{
		auto next = callbacks->_next;
		callbacks->_complete(callbacks->_object);
		callbacks = next;
	}
}

NAGISA_BUILD_LIB_DETAIL_END

NAGISA_BUILD_LIB_BEGIN

secondary_command_recording::secondary_command_recording(
	::std::shared_ptr<details::render_frame_state> state,
	::std::size_t generation_index,
	secondary_command_pool::command_buffer command
) noexcept
	: _state(::std::move(state))
	, _generation_index(generation_index)
	, _command(::std::move(command))
{}

secondary_command_retirement::secondary_command_retirement(secondary_command_recording recording) noexcept
	: _recording(::std::move(recording))
{}

render_recording_frame::render_recording_frame(frame_vulkan_env_renderer renderer,
	::std::shared_ptr<details::render_frame_state> state,
	secondary_command_pool& commands) noexcept
	: frame_vulkan_env_renderer(renderer), _state(::std::move(state)), _commands(&commands)
{
}

render_recording_frame::operator bool() const noexcept
{
	return static_cast<bool>(_state);
}

auto render_recording_frame::allocate() const -> secondary_command_recording
{
	assert(_state);
	assert(_commands != nullptr);
	auto command = _commands->acquire();
	auto generation_index = ::std::size_t{};
	{
		auto lock = ::std::scoped_lock{_state->_mutex};
		assert(!_state->_completed);
		generation_index = _state->_secondary_commands.size();
		_state->_secondary_commands.push_back({._handle = command.get()});
	}
	return secondary_command_recording{_state, generation_index, ::std::move(command)};
}

auto render_recording_frame::retire(secondary_command_recording command) const noexcept -> secondary_command_retirement
{
	assert(_state);
	assert(_commands != nullptr);
	assert(command._state == _state);
	assert(command._command._pool == _commands);
	return secondary_command_retirement{::std::move(command)};
}

auto render_workflow::async_record_awaitable::await_suspend(::std::coroutine_handle<> parent) -> bool
{
	assert(_workflow != nullptr);
	assert(_commands != nullptr);
	_parent = parent;
	auto lock = ::std::scoped_lock{_workflow->_waiters_mutex};
	if (_workflow->_stop_source.stop_requested())
	{
		return false;
	}
	_workflow->_waiters.push_back(this);
	return true;
}

auto render_workflow::async_record_awaitable::await_resume() noexcept -> render_recording_frame
{
	return ::std::move(_frame);
}

render_workflow::render_workflow(vulkan_context& renderer_) noexcept : renderer(renderer_) {}

auto render_workflow::get_scheduler() noexcept -> ::exec::static_thread_pool::scheduler
{
	return _inner_pool.get_scheduler();
}

auto render_workflow::wait_for_submit() -> void
{
	(void)::stdexec::sync_wait(_inner_scope.on_empty());
	(void)::stdexec::sync_wait(::stdexec::starts_on(get_scheduler(), ::stdexec::just()));
}

auto render_workflow::request_stop() noexcept -> void
{
	auto lock = ::std::scoped_lock{_waiters_mutex};
	_stop_source.request_stop();
}

auto render_workflow::finish() noexcept -> void
{
	_inner_pool.request_stop();
	_recording_pool.request_stop();
}

auto render_workflow::submit() -> void
{
	constexpr auto frame_wait_timeout = ::std::uint64_t{16'000'000};
	(void)::stdexec::sync_wait(_inner_scope.on_empty());
	auto previous_submit_error = ::std::exception_ptr{};
	{
		auto lock = ::std::scoped_lock{_submit_error_mutex};
		previous_submit_error = ::std::exchange(_submit_error, {});
	}

	if (_stop_source.stop_requested())
	{
		auto stopped_waiters = ::std::vector<async_record_awaitable*>{};
		auto stop_error = ::std::exception_ptr{};
		{
			auto lock = ::std::scoped_lock{_waiters_mutex};
			stopped_waiters.swap(_waiters);
		}

		for (auto waiter : stopped_waiters)
		{
			try
			{
				_inner_scope.spawn(::stdexec::starts_on(get_scheduler(), ::stdexec::just(waiter))
					| ::stdexec::then(
						[this](async_record_awaitable* current) noexcept
						{
							auto handle = current->_parent;
							if (handle && !handle.done())
							{
								try
								{
									handle.resume();
								}
								catch (...)
								{
									auto error = ::std::current_exception();
									auto lock = ::std::scoped_lock{_submit_error_mutex};
									if (!_submit_error)
									{
										_submit_error = ::std::move(error);
									}
								}
							}
						}));
			}
			catch (...)
			{
				if (!stop_error)
				{
					stop_error = ::std::current_exception();
				}

				auto handle = waiter->_parent;
				if (handle && !handle.done())
				{
					try
					{
						handle.resume();
					}
					catch (...)
					{
						if (!stop_error)
						{
							stop_error = ::std::current_exception();
						}
					}
				}
			}
		}
		(void)::stdexec::sync_wait(_inner_scope.on_empty());
		{
			auto lock = ::std::scoped_lock{_submit_error_mutex};
			auto async_stop_error = ::std::exchange(_submit_error, {});
			if (!stop_error)
			{
				stop_error = ::std::move(async_stop_error);
			}
		}

		if (previous_submit_error)
		{
			::std::rethrow_exception(previous_submit_error);
		}
		if (stop_error)
		{
			::std::rethrow_exception(stop_error);
		}
		return;
	}

	if (previous_submit_error)
	{
		::std::rethrow_exception(previous_submit_error);
	}

	auto&& r = renderer;

	//+ begin frame
	{
		auto&& slot = r._slots[_current_slot];

		if (r._resize_requested.exchange(false, ::std::memory_order_acq_rel))
		{
			if (r._requested_extent.width == 0 || r._requested_extent.height == 0)
			{
				return;
			}

			::bvn::graphics::resize(r, r._requested_extent);
		}

		auto const fence_result =
			::vkWaitForFences(r._device.handle, 1, &slot.in_flight.handle, VK_TRUE, frame_wait_timeout);
		if (fence_result == ::VK_TIMEOUT)
		{
			return;
		}
		if (fence_result != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to wait for in-flight fence"};
		}

		if (auto completed = ::std::exchange(_submitted_frames[_current_slot], {}))
		{
			completed->complete();
		}

		auto acquire_result = ::vkAcquireNextImageKHR(r._device.handle,
			r._swapchain.handle,
			frame_wait_timeout,
			slot.image_available.handle,
			VK_NULL_HANDLE,
			&_active_image_index);
		if (acquire_result == ::VK_TIMEOUT || acquire_result == ::VK_NOT_READY)
		{
			return;
		}

		if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			auto extent = r._target_window != nullptr
				? r._target_window->drawable_extent()
				: ::bvn::platform::window_extent{r._swapchain_extent.width, r._swapchain_extent.height};
			if (extent.width != 0 && extent.height != 0)
			{
				::bvn::graphics::resize(r, extent);
			}
			return;
		}

		if (acquire_result == VK_SUBOPTIMAL_KHR)
		{
			r._requested_extent = r._target_window != nullptr
				? r._target_window->drawable_extent()
				: ::bvn::platform::window_extent{r._swapchain_extent.width, r._swapchain_extent.height};
			r._resize_requested.store(true, ::std::memory_order_release);
		}

		if (acquire_result != ::VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR)
		{
			throw ::std::runtime_error{"failed to acquire swapchain image"};
		}

		if (::vkResetFences(r._device.handle, 1, &slot.in_flight.handle) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to reset in-flight fence"};
		}

		if (::vkResetCommandPool(r._device.handle, slot.primary_command_pool.handle, 0) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to reset frame command pool"};
		}

		auto begin_info = VkCommandBufferBeginInfo{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (::vkBeginCommandBuffer(slot.primary_command_buffer, &begin_info) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to begin frame command buffer"};
		}
	}

	//+ begin rendering
	{
		auto&& slot = r._slots[_current_slot];
		auto command = slot.primary_command_buffer;

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
			barrier.image = r._swapchain_images[_active_image_index];
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

		{
			auto barrier = VkImageMemoryBarrier2{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
			barrier.srcAccessMask = 0;
			barrier.dstStageMask =
				VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
			barrier.dstAccessMask =
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = slot.depth_image._image.handle;
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
		color_attachment.imageView = r._swapchain_image_views[_active_image_index].handle;
		color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.clearValue = clear_color;

		auto depth_attachment = VkRenderingAttachmentInfo{};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = slot.depth_image._view.handle;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.clearValue = clear_depth;

		auto render_area = VkRect2D{};
		render_area.offset = {0, 0};
		render_area.extent = r._swapchain_extent;

		auto rendering_info = VkRenderingInfo{};
		rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering_info.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
		rendering_info.renderArea = render_area;
		rendering_info.layerCount = 1;
		rendering_info.colorAttachmentCount = 1;
		rendering_info.pColorAttachments = &color_attachment;
		rendering_info.pDepthAttachment = &depth_attachment;

		::vkCmdBeginRendering(command, &rendering_info);
	}

	auto frame_state = ::std::make_shared<details::render_frame_state>();
	auto frame_waiters = ::std::vector<async_record_awaitable*>{};
	auto recovery_waiters = ::std::vector<async_record_awaitable*>{};
	{
		auto lock = ::std::scoped_lock{_waiters_mutex};
		recovery_waiters = _waiters;
		frame_waiters.swap(_waiters);
	}

	auto const slot_index = _current_slot;
	auto const active_image_index = _active_image_index;
	for (auto waiter : frame_waiters)
	{
		assert(waiter != nullptr);
		waiter->_frame = render_recording_frame{
			frame_vulkan_env_renderer{
				._context = &renderer,
				._slot_index = slot_index,
				._active_image_index = active_image_index,
			},
			frame_state,
			*waiter->_commands,
		};
	}

	auto const frame_waiter_count = frame_waiters.size();
	try
	{
		auto work = ::stdexec::just(::std::move(frame_waiters))
			| ::stdexec::continues_on(_recording_pool.get_scheduler())
			| ::stdexec::bulk(::stdexec::par,
				frame_waiter_count,
				[this](::std::size_t index, auto&& current_waiters) noexcept
				{
					auto waiter = current_waiters[index];
					auto handle = waiter->_parent;
					if (handle && !handle.done())
					{
						try
						{
							handle.resume();
						}
						catch (...)
						{
							auto error = ::std::current_exception();
							auto lock = ::std::scoped_lock{_submit_error_mutex};
							if (!_submit_error)
							{
								_submit_error = ::std::move(error);
							}
						}
					}
				})
			| ::stdexec::continues_on(get_scheduler())
			| ::stdexec::then(
				[this, slot_index, active_image_index, frame_state](auto&&) noexcept
				{
					auto queue_submitted = false;
					try
					{
						auto&& r = renderer;
						auto&& primary_slot = r._slots[slot_index];
						auto secondaries = ::std::vector<VkCommandBuffer>{};
						{
							auto lock = ::std::scoped_lock{frame_state->_mutex};
							secondaries.reserve(frame_state->_secondary_commands.size());
							for (auto&& command : frame_state->_secondary_commands)
							{
								if (command._published)
								{
									secondaries.push_back(command._handle);
								}
							}
						}

						if (!secondaries.empty())
						{
							::vkCmdExecuteCommands(primary_slot.primary_command_buffer,
								static_cast<::std::uint32_t>(secondaries.size()),
								secondaries.data());
						}

						//+ end rendering
						{
							::vkCmdEndRendering(primary_slot.primary_command_buffer);
						}

						//+ end frame
						{
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
								barrier.image = r._swapchain_images[active_image_index];
								barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
								barrier.subresourceRange.baseMipLevel = 0;
								barrier.subresourceRange.levelCount = 1;
								barrier.subresourceRange.baseArrayLayer = 0;
								barrier.subresourceRange.layerCount = 1;

								auto dependency = VkDependencyInfo{};
								dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
								dependency.imageMemoryBarrierCount = 1;
								dependency.pImageMemoryBarriers = &barrier;
								::vkCmdPipelineBarrier2(primary_slot.primary_command_buffer, &dependency);
							}

							if (::vkEndCommandBuffer(primary_slot.primary_command_buffer) != ::VK_SUCCESS)
							{
								throw ::std::runtime_error{"failed to end frame command buffer"};
							}

							auto signal_semaphore = r._render_finished[active_image_index].handle;

							auto wait_info = VkSemaphoreSubmitInfo{};
							wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
							wait_info.semaphore = primary_slot.image_available.handle;
							wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

							auto command_info = VkCommandBufferSubmitInfo{};
							command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
							command_info.commandBuffer = primary_slot.primary_command_buffer;

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

							if (::vkQueueSubmit2(r._graphics_queue, 1, &submit_info, primary_slot.in_flight.handle)
								!= ::VK_SUCCESS)
							{
								throw ::std::runtime_error{"failed to submit graphics queue"};
							}

							assert(!_submitted_frames[slot_index]);
							_submitted_frames[slot_index] = frame_state;
							queue_submitted = true;

							auto present_info = VkPresentInfoKHR{};
							present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
							present_info.waitSemaphoreCount = 1;
							present_info.pWaitSemaphores = &signal_semaphore;
							present_info.swapchainCount = 1;
							auto swapchain = r._swapchain.handle;
							present_info.pSwapchains = &swapchain;
							present_info.pImageIndices = &active_image_index;

							auto present_result = ::vkQueuePresentKHR(r._present_queue, &present_info);

							_current_slot = (slot_index + 1) % ::bvn::graphics::frames_in_flight;

							if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
							{
								auto extent = r._target_window != nullptr
									? r._target_window->drawable_extent()
									: ::bvn::platform::window_extent{
										  r._swapchain_extent.width, r._swapchain_extent.height};
								if (extent.width != 0 && extent.height != 0)
								{
									::bvn::graphics::resize(r, extent);
								}
							}
							else if (present_result != ::VK_SUCCESS)
							{
								throw ::std::runtime_error{"failed to present swapchain image"};
							}
						}
					}
					catch (...)
					{
						if (!queue_submitted)
						{
							frame_state->complete();
						}

						auto error = ::std::current_exception();
						auto lock = ::std::scoped_lock{_submit_error_mutex};
						if (!_submit_error)
						{
							_submit_error = ::std::move(error);
						}
					}
				});

		_inner_scope.spawn(::std::move(work));
	}
	catch (...)
	{
		auto error = ::std::current_exception();
		for (auto waiter : recovery_waiters)
		{
			auto handle = waiter->_parent;
			if (handle && !handle.done())
			{
				try
				{
					handle.resume();
				}
				catch (...)
				{
					auto resume_error = ::std::current_exception();
					auto lock = ::std::scoped_lock{_submit_error_mutex};
					if (!_submit_error)
					{
						_submit_error = ::std::move(resume_error);
					}
				}
			}
		}
		frame_state->complete();
		::std::rethrow_exception(error);
	}
}

auto render_workflow::complete_submissions() noexcept -> void
{
	for (auto&& submitted : _submitted_frames)
	{
		if (auto completed = ::std::exchange(submitted, {}))
		{
			completed->complete();
		}
	}
}

NAGISA_BUILD_LIB_END

#include <nagisa/build_lib/destruct.h>
