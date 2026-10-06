#pragma once

#include <cassert>
#include <coroutine>
#include <list>
#include <mutex>
#include <stdexcept>

#include <vkkl/vkkl.h>
#include <bvn/graphics/renderer.h>

struct frame_slot
{
	::vkkl::fence _in_flight;
	::vkkl::command_pool _primary_command_pool;
	::vkkl::command_buffer _primary_command_buffer;
	::vkkl::semaphore _image_available;
	::vkkl::semaphore _render_finished;
	::std::uint32_t _active_image_index = 0;
	::VkImage _active_image = VK_NULL_HANDLE;
	::VkImageView _active_image_view = VK_NULL_HANDLE;
	::VkImage _depth_image = VK_NULL_HANDLE;
	::VkImageView _depth_image_view = VK_NULL_HANDLE;
	::VkExtent2D _extent{};

	constexpr auto in_flight() const noexcept { return _in_flight.handle; }
	constexpr auto primary_command_pool() const noexcept { return _primary_command_pool.handle; }
	constexpr auto primary_command_buffer() const noexcept { return _primary_command_buffer.handle; }
	constexpr auto image_available() const noexcept { return _image_available.handle; }
	constexpr auto render_finished() const noexcept { return _render_finished.handle; }
	constexpr auto active_image_index() const noexcept { return _active_image_index; }
	constexpr auto active_image() const noexcept { return _active_image; }
	constexpr auto active_image_view() const noexcept { return _active_image_view; }
	constexpr auto depth_image() const noexcept { return _depth_image; }
	constexpr auto depth_image_view() const noexcept { return _depth_image_view; }
	constexpr auto extent() const noexcept { return _extent; }
};