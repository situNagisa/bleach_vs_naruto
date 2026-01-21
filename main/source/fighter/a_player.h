#pragma once

#include <SDL2/SDL.h>
#include <vector>
#include <stdexcept>
#include <stb_image.h>
#include <boost/sml.hpp>
#include <unordered_map>

#include "../sdl_graphic.h"

#include "./player.h"

namespace a_impl
{
	struct GifFrame {
		SDL_Texture* tex;
		::std::chrono::milliseconds delay;
	};

	struct GifPlayer {
		std::vector<GifFrame> frames{};
		::std::chrono::milliseconds delay{};
		::std::size_t current = 0;

		void update(::std::chrono::milliseconds delta)
		{
			if (frames.empty())
				return;
			if (delay > delta)
			{
				delay -= delta;
				return;
			}
			while (true)
			{
				delta -= delay;
				current = (current + 1) % frames.size();
				delay = frames[current].delay;
				if (delay > delta)
				{
					delay -= delta;
					break;
				}
			}
		}
	};

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
		float x{}, y{};
		float vx{}, vy{};
		int speed = 1;
		
		::std::unordered_map<gif_type, GifPlayer> gif_pool{
			{gif_type::idle, {}},
			{gif_type::a1, {}},
		};
		GifPlayer* current{};

		static void move(events::move m, context& self)
		{
			self.x += m.dx * self.speed;
			self.y += m.dy * self.speed;
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

	inline void load_gif(
		SDL_Renderer* renderer,
		const char* path,
		GifPlayer& out_player
	) {
		auto f = ::std::fopen(path, "rb");
		if (!f)
			throw ::std::runtime_error("fail to open file");
		::std::fseek(f, 0, SEEK_END);
		auto size = ::std::ftell(f);
		::std::rewind(f);

		::std::vector<unsigned char> fileData(size);
		::std::fread(fileData.data(), 1, size, f);
		::std::fclose(f);

		int* delays = nullptr;
		int width, height, frames;
		unsigned char* pixels = stbi_load_gif_from_memory(
			fileData.data(), size,
			&delays,
			&width, &height,
			&frames,
			nullptr, 4
		);

		if (!pixels)
			throw ::std::runtime_error("Failed to load gif");

		for (int i = 0; i < frames; i++) {
			auto tex = SDL_CreateTexture(
				renderer,
				SDL_PIXELFORMAT_RGBA32,
				SDL_TEXTUREACCESS_STATIC,
				width, height
			);

			SDL_UpdateTexture(
				tex,
				nullptr,
				pixels + i * width * height * 4,
				width * 4
			);

			out_player.frames.emplace_back(tex, ::std::chrono::milliseconds(delays[i]));
		}
		::stbi_image_free(pixels);
		::stbi_image_free(delays);
	}
}

struct a_player : player
{
	a_impl::context c{};
	::boost::sml::sm<a_impl::state_machine> sm{ c };
	a_player()
	{
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
	bool loaded = false;
	void process_graphic(graphic::renderer& renderer) override
	{
		if (!::std::exchange(loaded, true))
		{
			auto r = static_cast<sdl_renderer&>(renderer)._window.renderer;
			a_impl::load_gif(r, "default.gif", c.gif_pool[a_impl::gif_type::idle]);
			a_impl::load_gif(r, "1a.gif", c.gif_pool[a_impl::gif_type::a1]);
		}
		if (c.current)
		{
			renderer.draw_texture(c.current->frames[c.current->current].tex, c.x, c.y);
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
				if (c.current->current == c.current->frames.size() - 1)
				{
					c.current->current = 0;
					sm.process_event(ee::idle{});
				}
			}
		}
		auto dt = delta.count() / 1000.0f;
		constexpr auto gravity = 3000.0f;
		c.vy += gravity * dt;
		c.y += c.vy * dt;
		constexpr auto ground_y = 500.0f;
		if (c.y >= ground_y)
		{
			c.y = ground_y;
			c.vy = 0.0f;
		}
	}
};
