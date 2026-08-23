
#include <chrono>
#include <memory>
#include <ranges>
#include <latch>
#include <coroutine>
#include <cstddef>
#include <algorithm>
#include <array>
#include <cassert>
#include <iterator>
#include <list>
#include <mutex>
#include <span>
#include <tuple>
#include <utility>
#include <vector>
#include <print>
#include <thread>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <entt/entt.hpp>

#include <bvn/platform/sdl_context.h>
#include <bvn/platform/window.h>

#include "./barrier.h"
#include "./consumer_task.h"
#include "./demo_vulkan.h"

using namespace ::std::chrono_literals;

using vulkan_context = ::consumer_arch_vulkan::vulkan_context;


struct render_begin_t : private barrier
{
	using base_type = barrier;
	using self_type = render_begin_t;
	using base_type::base_type;
	using base_type::wait;

	[[nodiscard]] auto arrive_and_wait() noexcept
	{
		struct awaitable
		{
			[[nodiscard]] constexpr static auto await_ready() noexcept { return false; }

			::std::coroutine_handle<> await_suspend(::std::coroutine_handle<> waiter) const noexcept
			{
				assert(_self);
				auto lock = ::std::scoped_lock{ _self->_mutex };
				assert(_self->_waiters.size() < _self->_expected);
				_self->_waiters.push_back(waiter);
				if (_self->_waiters.size() == _self->_expected)
					return _self->_completion;
				return ::std::noop_coroutine();
			}
			constexpr auto const& await_resume() const noexcept { return _self->_slot; }

			self_type* _self = nullptr;
		};
		return awaitable{ ._self = this };
	}

	::std::shared_ptr<::bvn::graphics::frame_dynamic_forward_env_renderer> _slot;
};
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
			throw ::std::runtime_error{"failed to acquire swapchain image"};
		}

		auto const images = _renderer.swapchain_images();
		auto const views = _renderer.swapchain_image_views();
		if (slot._active_image_index >= images.size() || slot._active_image_index >= views.size())
		{
			throw ::std::runtime_error{"invalid acquired swapchain image index"};
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
		constexpr auto release() const noexcept
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

			auto await_suspend(::std::coroutine_handle<> waiter) noexcept
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
		::std::list<::std::tuple<::std::coroutine_handle<>, slot_type**>> waiters{};
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
		auto device = ::vkkl::device_observer{renderer.device()};
		for (auto index = ::std::size_t{}; index < count; ++index)
		{
			auto& slot = _free_slots.emplace_back();
			slot._primary_command_pool = device.create_command_pool(::vkfu::unpack(::vkfu::evaluate(::vkfu::param::command_pool{
				.flags = {.transient = 1, .reset_command_buffer = 1},
				.queue_family_index = renderer.graphics_queue_family(),
				})));

			auto allocate_info = ::VkCommandBufferAllocateInfo{};
			allocate_info.sType = ::VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocate_info.commandPool = slot._primary_command_pool.handle;
			allocate_info.level = ::VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocate_info.commandBufferCount = 1;
			auto raw_command_buffer = ::VkCommandBuffer{};
			::consumer_arch_vulkan::check(
				::vkAllocateCommandBuffers(renderer.device(), &allocate_info, &raw_command_buffer),
				"failed to allocate primary command buffer"
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


struct context
{
	context()
		: frame_slots(vulkan.global_env(), 1)
	{}

	::exec::static_thread_pool thread_pool{20};
	// vulkan global context
	::bvn::platform::sdl_context sdl;
	::bvn::platform::window window{"vkkl Vulkan secondary triangle", 960, 540};
	vulkan_context vulkan{window};
	frame_slot_resource frame_slots;
};

struct get_request_render_begin_t
{
	consteval static auto query(::stdexec::forwarding_query_t) noexcept { return true; }
	template<class... Envs>
	constexpr auto operator()(::stdexec::env<Envs...> const& env) const noexcept -> bool
	{
		return static_cast<bool>(env.query(*this));
	}
	constexpr decltype(auto) operator()(auto const& env) const
		noexcept(noexcept(env.query(*this)))
	{
		if constexpr(requires{ env.query(*this); })
		{
			return env.query(*this);
		}
		else
		{
			return false;
		}
	}
};
inline constexpr get_request_render_begin_t get_request_render_begin{};
struct get_request_render_end_t
{
	consteval static auto query(::stdexec::forwarding_query_t) noexcept { return true; }
	template<class... Envs>
	constexpr auto operator()(::stdexec::env<Envs...> const& env) const noexcept -> bool
	{
		return static_cast<bool>(env.query(*this));
	}
	constexpr decltype(auto) operator()(auto const& env) const
		noexcept(noexcept(env.query(*this)))
	{
		if constexpr (requires{ env.query(*this); })
		{
			return env.query(*this);
		}
		else
		{
			return false;
		}
	}
};
inline constexpr get_request_render_end_t get_request_render_end{};

struct frame_context
{
	render_begin_t render_begin;
	barrier render_end;
	::std::mutex secondary_mutex{};
	::std::vector<::VkCommandBuffer> secondary_commands{};
	::std::size_t frame_index;
};

struct entity
{
	entity(context& c)
		: _game_context(c)
		, _secondary_command_pool(::consumer_arch_vulkan::create_secondary_command_pool(c.vulkan.global_env()))
	{}
	constexpr auto get_env() const noexcept
	{
		return ::stdexec::env{
			::stdexec::prop{get_request_render_begin, true}, 
			::stdexec::prop{get_request_render_end, true},
		};
	}
	virtual consumer_task run_once(frame_context& fc)
	{
		auto start_scheduler = co_await ::stdexec::read_env(::stdexec::get_scheduler);
		::std::println("entity {}: arrive render_begin", fc.frame_index);
		auto&& slot = co_await (fc.render_begin.arrive_and_wait() | ::stdexec::continues_on(start_scheduler));
		::std::println("entity {}: resume from render_begin", fc.frame_index);
		assert(slot);
		auto const global_renderer = _game_context.vulkan.global_env();
		auto secondary_command_buffer = ::consumer_arch_vulkan::record_triangle(
			global_renderer,
			*slot,
			_secondary_command_pool
		);
		{
			auto lock = ::std::scoped_lock{ fc.secondary_mutex };
			fc.secondary_commands.push_back(secondary_command_buffer.handle);
		}

		::std::println("entity {}: arrive render_end", fc.frame_index);
		co_await (fc.render_end.arrive_and_wait() | ::stdexec::continues_on(start_scheduler));
		::std::println("entity {}: resume from render_end", fc.frame_index);
	}
	context& _game_context;
	::vkkl::command_pool _secondary_command_pool;
};

inline constexpr auto exception_handler = [](auto&&) noexcept{::std::terminate();};

consumer_task run_frame(::std::latch& next_frame, context& game_context, ::std::span<entity*> entities, ::std::size_t frame_index)
{
	auto start_scheduler = co_await ::stdexec::read_env(::stdexec::get_scheduler);

	namespace vs = ::std::views;
	constexpr auto dereference = [](auto* e) noexcept -> auto& { return *e; };

	auto slot_scope = ::stdexec::simple_counting_scope{};
	auto slot_sender = ::stdexec::spawn_future(game_context.frame_slots.acquire(), slot_scope.get_token());
	
	auto fc = frame_context{
		.render_begin{static_cast<::std::size_t>(::std::ranges::count(entities | vs::transform(dereference) | vs::transform(&entity::get_env) | vs::transform(get_request_render_begin), true)) },
		.render_end{static_cast<::std::size_t>(::std::ranges::count(entities | vs::transform(dereference) | vs::transform(&entity::get_env) | vs::transform(get_request_render_end), true)) },
		.frame_index = frame_index,
	};
	auto compute_phase = ::stdexec::simple_counting_scope{};
	for (auto&& entity : entities | vs::transform(dereference))
	{
		::stdexec::spawn(::stdexec::starts_on(game_context.thread_pool.get_scheduler(), entity.run_once(fc)) | ::stdexec::upon_error(exception_handler), compute_phase.get_token());
	}
	::std::println("run_frame {}: await slot start", frame_index);
	auto [waiters, slot] = co_await (::stdexec::when_all(fc.render_begin.wait(), ::std::move(slot_sender)) | ::stdexec::continues_on(start_scheduler));
	// auto slot = co_await ::std::move(slot_sender);// | ::stdexec::continues_on(start_scheduler));
	// auto waiters = co_await fc.render_begin.wait();// | ::stdexec::continues_on(start_scheduler));
	::std::println("run_frame {}: await slot done", frame_index);
	auto dynamic_slot = ::bvn::graphics::dynamic_forward_frame_env_renderer(slot);
	::consumer_arch_vulkan::begin_frame(game_context.vulkan.global_env(), dynamic_slot);
	fc.render_begin._slot = ::std::make_shared<::bvn::graphics::frame_dynamic_forward_env_renderer>(::std::move(dynamic_slot));
	next_frame.count_down();
	for (auto waiter : waiters)
		waiter.resume();

	{
		::std::println("run_frame {}: await render_end start", frame_index);
		waiters = co_await (fc.render_end.wait() | ::stdexec::continues_on(start_scheduler));
		::std::println("run_frame {}: await render_end done", frame_index);
		assert(fc.render_begin._slot);
		auto const renderer = game_context.vulkan.global_env();
		auto queue_lock = ::consumer_arch_vulkan::lock_temporary_queue_synchronization(renderer);
		auto const present_result = ::consumer_arch_vulkan::submit_present_frame(renderer, *fc.render_begin._slot, fc.secondary_commands);

		// This demo intentionally waits for the GPU synchronously in run_frame.
		::consumer_arch_vulkan::wait_for_frame_gpu(renderer, *fc.render_begin._slot);
		::consumer_arch_vulkan::check_present_result(present_result);

		for (auto waiter : waiters)
			waiter.resume();
	}
	co_await (compute_phase.join() | ::stdexec::write_env(::stdexec::prop(::stdexec::get_start_scheduler, game_context.thread_pool.get_scheduler())));
	co_await (slot_scope.join() | ::stdexec::write_env(::stdexec::prop(::stdexec::get_start_scheduler, game_context.thread_pool.get_scheduler())));
	::std::println("run_frame {}: slot release", frame_index);
	slot.release();
}

int main()
{
	auto game_context = context{};
	auto frames = ::stdexec::counting_scope{};
	auto entity1 = entity{ game_context };
	auto entity2 = entity{ game_context };
	auto entities = ::std::array{ &entity1, &entity2 };
	auto stop_source = ::stdexec::inplace_stop_source{};
	auto run_frame_slot = ::std::jthread{[&]{
			while (!stop_source.stop_requested())
			{
				game_context.frame_slots.run_once();
			}
		}};

	for ([[maybe_unused]] auto frame_index : ::std::views::iota(0u, 3u))
	{
		::std::println("main: {}", frame_index);
		::std::latch next_frame{ 1 };
		::stdexec::spawn(
			::stdexec::starts_on(game_context.thread_pool.get_scheduler(), run_frame(next_frame, game_context, entities, frame_index))
			| ::stdexec::upon_error(exception_handler)
			, frames.get_token()
		);
		
		next_frame.wait();
	}
	frames.close();
	::stdexec::sync_wait(frames.join());
	frames.request_stop();
	stop_source.request_stop();
}
