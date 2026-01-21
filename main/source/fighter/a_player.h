#pragma once

#include <SDL2/SDL.h>
#include <vector>
#include <stdexcept>
#include "./animation.h"
#include <boost/sml.hpp>
#include <unordered_map>

#include "../sdl_graphic.h"

#include "./player.h"
#include "./physical.h"
#include "./scene_context.h"

namespace a_impl
{
	// Use Animator from animation.h

	namespace states
	{
		struct idle {};
		struct a1 {};
		struct move{};
	}
	namespace events
	{
		struct attack{};
		struct move { int dx{}, dy{}; };
		struct idle{};
	}

	enum class gif_type
	{
		idle,
		a1,
	};

	struct context
	{
		::entt::registry* world{};
		::entt::entity entity{world->create()};
		float speed = 1.0f;
		
		::std::unordered_map<gif_type, animation_impl::Animator> gif_pool = []
			{
				::std::unordered_map<gif_type, animation_impl::Animator> result{};
				result.try_emplace(gif_type::idle, animation_impl::Animator{});
				result.try_emplace(gif_type::a1, animation_impl::Animator{});
				return result;
			}();
		animation_impl::Animator* current{};

		static void move(events::move m, context& self)
		{
			auto&& phys = self.world->get<physical_component>(self.entity);
			phys.position.x += m.dx * self.speed;
			phys.position.y += m.dy * self.speed;
		}
	};

	struct state_machine
	{
		auto operator()() const
		{
			using namespace ::boost::sml;

			constexpr auto set_player = [](gif_type t)
				{
					return [t](context& c)
						{
							c.current = &c.gif_pool.at(t);
						};
				};

			return make_transition_table(
				// *state<states::idle> +event<events::attack> = state<states::a1>
				*state<states::idle> + on_entry<_> / set_player(gif_type::idle)
				, state<states::idle> + event<events::attack> = state<states::a1>
				, state<states::idle> + event<events::move> = state<states::move>
				
				, state<states::move> + on_entry<_> / [](events::move const& m, context& c) { context::move(m, c); }
				, state<states::move> + event<events::attack>  = state<states::a1>
				, state<states::move> + event<events::idle> = state<states::idle>
				, state<states::move> + event<events::move> = state<states::move>
				
				, state<states::a1> + on_entry<_> / set_player(gif_type::a1)
				, state<states::a1> + event<events::idle> = state<states::idle>
			);
		}
	};

	// Loading is handled by animation_impl::load_gif_to_animator
}

struct a_player : player
{
	a_impl::context c{};
	::boost::sml::sm<a_impl::state_machine> sm{ c };
	a_player(fighter_scene_context& context, sdl_renderer& renderer)
		: c{ .world = &context.world, .entity = context.world.create() }
	{
		context.world.emplace<physical_component>(c.entity);
		// construct animator entries and load into them
		animation_impl::load_gif_to_animator(renderer._window.renderer, "default.gif", c.gif_pool.at(a_impl::gif_type::idle));
		animation_impl::load_gif_to_animator(renderer._window.renderer, "1a.gif", c.gif_pool.at(a_impl::gif_type::a1));
	}
	void process_io(io::keyboard& keyboard) override
	{
		using namespace ::boost::sml;
		namespace ss = a_impl::states;
		namespace ee = a_impl::events;

		ee::move m{};
		if (keyboard.a())
			m.dx -= 1;
		if (keyboard.d())
			m.dx += 1;
		if (keyboard.w())
			m.dy -= 1;
		if (keyboard.s())
			m.dy += 1;

		if (sm.is(state<ss::idle>))
		{
			if (m.dx || m.dy)
			{
				sm.process_event(m);
			}
			if (keyboard.j())
			{
				sm.process_event(ee::attack{});
			}
		}
		else if (sm.is(state<ss::move>))
		{
			if (m.dx || m.dy)
			{
				sm.process_event(m);
			}
			else
			{
				sm.process_event(ee::idle{});
			}
			if (keyboard.j())
			{
				sm.process_event(ee::attack{});
			}
		}
		else if (sm.is(state<ss::a1>))
		{

		}
	}
	void process_graphic(graphic::renderer& renderer) override
	{
		if (c.current)
		{
			auto& phys = c.world->get<physical_component>(c.entity);
			auto off = c.current->get_current_offset();
			int x = static_cast<int>(phys.position.x) + off.x;
			int y = static_cast<int>(phys.position.y) + off.y;
			renderer.draw_texture(c.current->get_current_texture(), x, y);
		}
	}
	void process_time(::std::chrono::milliseconds delta) override
	{
		using namespace ::boost::sml;
		namespace ss = a_impl::states;
		namespace ee = a_impl::events;

		if (c.current)
		{
			c.current->update(delta);
			if (sm.is(state<ss::a1>))
			{
				if (!c.current->frames.empty() && c.current->current == c.current->frames.size() - 1)
				{
					c.current->current = 0;
					sm.process_event(ee::idle{});
				}
			}
		}
	}
};
