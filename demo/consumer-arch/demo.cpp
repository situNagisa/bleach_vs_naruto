
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

#include "./frame_slot.h"

using vulkan_context = ::consumer_arch_vulkan::vulkan_context;




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
