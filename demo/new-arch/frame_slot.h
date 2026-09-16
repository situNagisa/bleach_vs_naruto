#pragma once

#include <cassert>
#include <coroutine>
#include <list>
#include <mutex>
#include <stdexcept>

#include <vkkl/vkkl.h>
#include <bvn/graphics/renderer.h>

#include "./demo_vulkan.h"
#include "./resource_pool.h"

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
static_assert(::bvn::graphics::frame_env_renderer<frame_slot>);

struct frame_slot_resource
{
	using slot_type = frame_slot;

	auto&& _free_to_busy() noexcept
	{
		auto slot = ::std::move(_free_slots.front());
		_free_slots.pop_front();
		return _busy_slots.emplace_back(::std::move(slot));
	}
	auto _busy_to_free(slot_type& slot) noexcept
	{
		auto it = ::std::ranges::find_if(_busy_slots, [&slot](slot_type const& s) { return &s == &slot; });
		assert(it != ::std::ranges::end(_busy_slots));
		_free_slots.splice(_free_slots.end(), _busy_slots, it);
	}

	auto _prepare_slot(slot_type& slot) -> void
	{
		auto acquire_result = ::vkAcquireNextImageKHR(
			_renderer.device(),
			_renderer.swapchain(),
			(::std::numeric_limits<::std::uint64_t>::max)(),
			slot._image_available.handle,
			VK_NULL_HANDLE,
			&slot._active_image_index
		);
		if (acquire_result != ::VK_SUCCESS && acquire_result != ::VK_SUBOPTIMAL_KHR)
		{
			throw ::std::runtime_error{ "failed to acquire swapchain image" };
		}

		auto const images = _renderer.swapchain_images();
		auto const views = _renderer.swapchain_image_views();
		if (slot._active_image_index >= images.size() || slot._active_image_index >= views.size())
		{
			throw ::std::runtime_error{ "invalid acquired swapchain image index" };
		}
		slot._active_image = images[slot._active_image_index];
		slot._active_image_view = views[slot._active_image_index];
		slot._extent = _renderer.swapchain_extent();
	}

	struct forward_slot_type : ::bvn::graphics::frame_forward_env_renderer<slot_type*>
	{
		using base_type = ::bvn::graphics::frame_forward_env_renderer<slot_type*>;

		forward_slot_type(frame_slot_resource& self, slot_type& slot) noexcept
			: base_type(&slot)
			, _self(&self)
		{
		}
		forward_slot_type(forward_slot_type const&) = delete;
		auto operator=(forward_slot_type const&) -> forward_slot_type & = delete;
		forward_slot_type(forward_slot_type&& other) noexcept
			: base_type(::std::exchange(other._inner, nullptr))
			, _self(::std::exchange(other._self, nullptr))
		{
		}
		auto operator=(forward_slot_type&& other) noexcept -> forward_slot_type & = delete;
		~forward_slot_type() noexcept
		{
			if (_self)
				release();
		}

		void release() const noexcept
		{
			auto lock = ::std::scoped_lock{ _self->_mutex };
			_self->_busy_to_free(*base_type::handle());
		}
		frame_slot_resource* _self = nullptr;
	};

	constexpr auto acquire() noexcept
	{
		struct awaitable
		{
			[[nodiscard]] constexpr static auto await_ready() noexcept { return false; }

			auto await_suspend(std::coroutine_handle<> waiter) noexcept
			{
				assert(_self);
				auto lock = ::std::scoped_lock{ _self->_mutex };
				_self->_waiters.emplace_back(waiter, &_slot);
			}
			auto await_resume() const
			{
				assert(_slot);
				_self->_prepare_slot(*_slot);
				return forward_slot_type{ *_self, *_slot };
			}
			frame_slot_resource* _self;
			slot_type* _slot{ nullptr };
		};
		return awaitable{ ._self = this };
	}

	auto run_once()
	{
		std::list<::std::tuple<::std::coroutine_handle<>, slot_type**>> waiters{};
		{
			auto lock = ::std::scoped_lock{ _mutex };
			auto const count = (::std::min)(_waiters.size(), _free_slots.size());
			auto waiter_end = ::std::next(_waiters.begin(), static_cast<::std::ptrdiff_t>(count));
			waiters.splice(waiters.end(), _waiters, _waiters.begin(), waiter_end);

			auto first_slot = _free_slots.begin();
			auto slot_end = ::std::next(first_slot, static_cast<::std::ptrdiff_t>(count));
			_busy_slots.splice(_busy_slots.end(), _free_slots, first_slot, slot_end);

			auto slot = first_slot;
			for (auto&& [waiter, output] : waiters)
			{
				*output = ::std::addressof(*slot);
				++slot;
			}
		}
		for (auto&& [waiter, slot] : waiters)
			waiter.resume();
	}

	frame_slot_resource(
		::consumer_arch_vulkan::global_vulkan_env_renderer renderer,
		::std::size_t count
	)
		: _renderer(renderer)
	{
		auto device = ::vkkl::device_observer{ renderer.device() };
		for (auto index = ::std::size_t{}; index < count; ++index)
		{
			auto& slot = _free_slots.emplace_back();
			slot._primary_command_pool = device.create_command_pool(::vkfu::unpack(::vkfu::evaluate(::vkfu::param::command_pool{
				.flags = {.transient = 1, .reset_command_buffer = 1},
				.queue_family_index = renderer.graphics_queue_family(),
				})));

			auto raw_command_buffer = ::VkCommandBuffer{};
			::vkfu::allocate_command_buffers(
				renderer.device(),
				::vkfu::param::command_buffer{
					.command_pool = slot._primary_command_pool.handle,
					.level = ::vkfu::enums::command_buffer_level::primary,
					.command_buffer_count = 1,
				},
				::std::span{ &raw_command_buffer, 1u }
				);
			slot._primary_command_buffer = ::vkkl::command_buffer{
				renderer.device(),
				slot._primary_command_pool.handle,
				raw_command_buffer,
			};

			auto semaphore_info = ::vkfu::evaluate(::vkfu::param::semaphore{});
			slot._image_available = device.create_semaphore(::vkfu::unpack(semaphore_info));
			slot._render_finished = device.create_semaphore(::vkfu::unpack(semaphore_info));
			auto fence_info = ::vkfu::evaluate(::vkfu::param::fence{});
			slot._in_flight = device.create_fence(::vkfu::unpack(fence_info));
		}
	}

	::consumer_arch_vulkan::global_vulkan_env_renderer _renderer;
	::std::mutex _mutex{};
	::std::list<::std::tuple<::std::coroutine_handle<>, slot_type**>> _waiters{};
	::std::list<slot_type> _free_slots{};
	::std::list<slot_type> _busy_slots{};
};