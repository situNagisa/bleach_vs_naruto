
#include <memory>
#include <ranges>
#include <latch>
#include <coroutine>
#include <cstddef>
#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <iterator>
#include <list>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <typeinfo>
#include <utility>
#include <vector>
#include <print>
#include <cstdio>
#include <thread>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>
#include <exec/finally.hpp>
#include <exec/static_thread_pool.hpp>
#include <SDL3/SDL_events.h>

#include <entt/entt.hpp>

#include <bvn/platform/sdl_context.h>
#include <bvn/platform/window.h>

#include "./consumer_task.h"
#include "./demo_vulkan.h"
#include "./job.h"
#include "../task-graph/dynamic_when_all.h"
#include "../task-graph/node_sender.h"

using vulkan_context = ::consumer_arch_vulkan::vulkan_context;


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
		forward_slot_type(forward_slot_type const&) = delete;
		auto operator=(forward_slot_type const&) -> forward_slot_type& = delete;
		forward_slot_type(forward_slot_type&& other) noexcept
			: base_type(::std::exchange(other._inner, nullptr))
			, _self(::std::exchange(other._self, nullptr))
		{}
		auto operator=(forward_slot_type&& other) noexcept -> forward_slot_type& = delete;
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

			auto raw_command_buffer = ::VkCommandBuffer{};
			::vkfu::allocate_command_buffers(
				renderer.device(),
				::vkfu::param::command_buffer{
					.command_pool = slot._primary_command_pool.handle,
					.level = ::vkfu::enums::command_buffer_level::primary,
					.command_buffer_count = 1,
				},
				::std::span{&raw_command_buffer, 1u}
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

struct frame_context
{
	::std::mutex secondary_mutex{};
	::std::vector<::vkkl::command_buffer> secondary_commands{};
	::std::size_t frame_index;
	::std::latch* next_frame = nullptr;

	::std::vector<::std::unique_ptr<consumer_arch_task_graph::job_base>> _jobs{};
	::std::vector<::bvn::task_graph::node_sender> _roots{};

	auto add(::bvn::task_graph::node_sender node) -> void
	{
		_roots.push_back(::std::move(node));
	}

	auto add_job(::std::unique_ptr<consumer_arch_task_graph::job_base> erased) -> void
	{
		assert(erased);
		assert(::std::ranges::none_of(_jobs, [incoming = erased.get()](auto const& existing)
			{
				return typeid(*existing) == typeid(*incoming);
			}) && "consumer-arch: duplicate job type");
		_jobs.push_back(::std::move(erased));
	}

	template <consumer_arch_task_graph::graph_job Job, class... Arguments>
	auto add_job(Arguments&&... arguments) -> Job&
	{
		assert(!job<Job>().has_value() && "consumer-arch: duplicate job type");
		auto erased = consumer_arch_task_graph::erase_job(
			::std::in_place_type<Job>, ::std::forward<Arguments>(arguments)...);
		auto& value = static_cast<consumer_arch_task_graph::details::erase_job<Job>&>(*erased)._value;
		add_job(::std::move(erased));
		return value;
	}

	template <consumer_arch_task_graph::graph_job Job>
	[[nodiscard]] auto job() noexcept -> ::std::optional<::std::reference_wrapper<Job>>
	{
		for (auto& erased : _jobs)
		{
			if (auto* value = dynamic_cast<consumer_arch_task_graph::details::erase_job<Job>*>(erased.get()))
				return ::std::ref(value->_value);
		}
		return ::std::nullopt;
	}

	// C++23 的 optional 不支持引用；C++26 增加 optional<T&> 偏特化。
	// 这里保持 C++23 兼容，返回的引用包装器只在本帧上下文存活期间有效。
	template <consumer_arch_task_graph::graph_job Job>
	[[nodiscard]] auto job() const noexcept -> ::std::optional<::std::reference_wrapper<Job const>>
	{
		for (auto const& erased : _jobs)
		{
			if (auto const* value = dynamic_cast<consumer_arch_task_graph::details::erase_job<Job> const*>(erased.get()))
				return ::std::cref(value->_value);
		}
		return ::std::nullopt;
	}

	auto build_jobs() -> void
	{
		for (auto& erased : _jobs)
			erased->build();
	}
};

inline constexpr auto exception_handler = [](auto&&) noexcept{::std::terminate();};

using render_begin_value = ::std::shared_ptr<::bvn::graphics::frame_dynamic_forward_env_renderer>;
using render_begin_completions = ::stdexec::completion_signatures<
	::stdexec::set_value_t(render_begin_value),
	::stdexec::set_error_t(::std::exception_ptr),
	::stdexec::set_stopped_t()>;
using render_begin_receiver = ::exec::any_receiver<render_begin_completions>;
using render_begin_sender = ::exec::any_sender<render_begin_receiver>;
using render_begin_split_sender = decltype(::exec::split(::std::declval<render_begin_sender>()));

struct render_entity
{
	using scheduler = ::exec::static_thread_pool::scheduler;

	struct job
	{
		render_entity& _entity;
		frame_context& _frame;
		scheduler _scheduler;
		::std::vector<::bvn::task_graph::node_sender> _recorders;
		::stdexec::simple_counting_scope _frame_scope;
		render_begin_split_sender _begin;
		render_begin_sender _end;
		::bvn::task_graph::node_sender _fence;

		job(render_entity& entity, frame_context& frame)
			: _entity(entity)
			, _frame(frame)
			, _scheduler(entity._context.thread_pool.get_scheduler())
			, _begin(::exec::split(render_begin_sender{
				::stdexec::starts_on(_scheduler,
					// A used scope must be joined before job destruction, even after its future completes.
					::exec::finally(
						::stdexec::spawn_future(_entity._context.frame_slots.acquire(), _frame_scope.get_token()),
						_frame_scope.join())
					| ::stdexec::then([this](auto slot) -> render_begin_value
						{
							auto dynamic_slot = ::bvn::graphics::dynamic_forward_frame_env_renderer(::std::move(slot));
							::consumer_arch_vulkan::begin_frame(_entity._context.vulkan.global_env(), dynamic_slot);
							auto frame = ::std::make_shared<::bvn::graphics::frame_dynamic_forward_env_renderer>(::std::move(dynamic_slot));
							assert(_frame.next_frame);
							_frame.next_frame->count_down();
							return frame;
						}))}))
			, _end(render_begin_sender{
				_begin
				| ::stdexec::let_value([this](render_begin_value const& frame)
					{
						return ::bvn::task_graph::dynamic_when_all(::std::move(_recorders))
							| ::stdexec::continues_on(_scheduler)
							| ::stdexec::then([this, frame]
								{
									auto const renderer = _entity._context.vulkan.global_env();
									auto queue_lock = ::consumer_arch_vulkan::lock_temporary_queue_synchronization(renderer);
									auto handles = _frame.secondary_commands
										| ::std::views::transform([](auto const& command) { return command.handle; })
										| ::std::ranges::to<::std::vector>();
									auto const present_result = ::consumer_arch_vulkan::submit_present_frame(renderer, *frame, handles);
									::consumer_arch_vulkan::check_present_result(present_result);
									return frame;
								});
					})})
			, _fence(::bvn::task_graph::make_node(
				::std::move(_end)
				| ::stdexec::then([this](render_begin_value frame)
					{
						wait_for_frame(*frame);
					})
				| ::stdexec::let_error([](::std::exception_ptr error)
					{
						return ::stdexec::just_error(::std::move(error));
					})
				| ::stdexec::let_stopped([]
					{
						return ::stdexec::just_stopped();
					})))
		{}

		auto add_recorder(::bvn::task_graph::node_sender recorder) -> void
		{
			_recorders.push_back(::std::move(recorder));
		}

		[[nodiscard]] auto begin_node() -> render_begin_split_sender
		{
			return _begin;
		}

		auto wait_for_frame(::bvn::graphics::frame_dynamic_forward_env_renderer const& frame) -> void
		{
			auto const renderer = _entity._context.vulkan.global_env();
			::consumer_arch_vulkan::wait_for_frame_gpu(renderer, frame);
			::std::println(stderr, "frame {}: presented, GPU complete", _frame.frame_index);
		}

		auto build() -> void
		{
			_frame.add(::std::move(_fence));
		}
	};

	explicit render_entity(context& context) noexcept
		: _context(context)
	{}

	auto begin_frame(frame_context& target) -> void
	{
		target.add_job<job>(*this, target);
	}

	context& _context;
};
template <::std::size_t EntityId>
struct entity
{
	using scheduler = ::exec::static_thread_pool::scheduler;
	static constexpr auto id = EntityId;

	entity(context& c)
		: _game_context(c)
		, _secondary_command_pool(::consumer_arch_vulkan::create_secondary_command_pool(c.vulkan.global_env()))
	{}

	auto record(frame_context& fc, render_begin_value const& frame) -> void
	{
		::std::println(stderr, "frame {}: entity {} record", fc.frame_index, id);
		auto const global_renderer = _game_context.vulkan.global_env();
		auto secondary_command_buffer = ::consumer_arch_vulkan::record_triangle(
			global_renderer,
			*frame,
			_secondary_command_pool
		);
		{
			auto lock = ::std::scoped_lock{ fc.secondary_mutex };
			fc.secondary_commands.push_back(::std::move(secondary_command_buffer));
		}
	}

	struct job
	{
		entity& _entity;
		frame_context& _frame;
		scheduler _scheduler;

		auto build() -> void
		{
			auto found = _frame.job<render_entity::job>();
			if (!found)
				throw ::std::runtime_error{"consumer-arch: render entity is not participating"};
			auto& render = found->get();
			render.add_recorder(::bvn::task_graph::make_node(
				render.begin_node()
				| ::stdexec::continues_on(_scheduler)
				| ::stdexec::then([this](render_begin_value const& frame) { _entity.record(_frame, frame); })
				| ::stdexec::then([] {})));
		}
	};

	auto begin_frame(frame_context& target) -> void
	{
		target.add_job<job>(*this, target, _game_context.thread_pool.get_scheduler());
	}

	context& _game_context;
	::vkkl::command_pool _secondary_command_pool;
};

struct basic_entity
{
	virtual ~basic_entity() = default;
	virtual auto begin_frame(frame_context& target) -> void = 0;
};

template <class Entity>
struct entity_holder final : basic_entity
{
	Entity* _entity;

	explicit entity_holder(Entity& value) noexcept
		: _entity(&value)
	{}

	auto begin_frame(frame_context& target) -> void override
	{
		_entity->begin_frame(target);
	}
};

consumer_task run_frame(
	::std::latch& next_frame,
	context& game_context,
	::std::span<::std::unique_ptr<basic_entity> const> entities,
	::std::size_t frame_index)
{
	if (entities.empty())
		throw ::std::runtime_error{"consumer-arch: render entity is required"};

	auto fc = frame_context{
		.frame_index = frame_index,
		.next_frame = &next_frame,
	};
	for (auto const& entity : entities)
		entity->begin_frame(fc);
	fc.build_jobs();
	co_await ::bvn::task_graph::dynamic_when_all(::std::move(fc._roots));
}

int main()
{
	auto game_context = context{};
	auto frames = ::stdexec::counting_scope{};
	auto renderer = render_entity{game_context};
	auto entity1 = entity<1>{ game_context };
	auto entity2 = entity<2>{ game_context };
	auto entities = ::std::vector<::std::unique_ptr<basic_entity>>{};
	entities.push_back(::std::make_unique<entity_holder<render_entity>>(renderer));
	entities.push_back(::std::make_unique<entity_holder<entity<1>>>(entity1));
	entities.push_back(::std::make_unique<entity_holder<entity<2>>>(entity2));
	auto stop_source = ::stdexec::inplace_stop_source{};
	auto run_frame_slot = ::std::jthread{[&]{
			while (!stop_source.stop_requested())
			{
				game_context.frame_slots.run_once();
			}
		}};

	auto event = ::SDL_Event{};
	for (auto frame_index : ::std::views::iota(0u, 4u))
	{
		::std::println(stderr, "frame {}: start", frame_index);
		::std::latch next_frame{ 1 };
		::stdexec::spawn(
			::stdexec::starts_on(game_context.thread_pool.get_scheduler(), run_frame(next_frame, game_context, entities, frame_index))
			| ::stdexec::upon_error(exception_handler),
			frames.get_token()
		);

		// Begin releases the next frame; keep pumping window events while it starts.
		while (!next_frame.try_wait())
		{
			::SDL_PollEvent(&event);
			::std::this_thread::yield();
		}
	}

	frames.close();
	::stdexec::sync_wait(frames.join());
	stop_source.request_stop();
	run_frame_slot.join();
	::std::println(stderr, "all 4 frames complete");
}
