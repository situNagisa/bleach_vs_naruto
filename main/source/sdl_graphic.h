#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "./graphic.h"

inline SDL_Texture* load_texture(SDL_Renderer* renderer, const char* path)
{
	static bool image_inited = false;
	if (!image_inited)
	{
		if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & (IMG_INIT_PNG | IMG_INIT_JPG)))
		{
			SDL_Log("IMG_Init failed: %s", IMG_GetError());
			return nullptr;
		}
		image_inited = true;
	}

	SDL_Surface* surface = IMG_Load(path);
	if (!surface)
	{
		SDL_Log("IMG_Load failed (%s): %s", path, IMG_GetError());
		return nullptr;
	}

	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_FreeSurface(surface);

	if (!texture)
	{
		SDL_Log("SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
		return nullptr;
	}

	return texture;
}


struct sdl_renderer : graphic::renderer
{
	struct canvas
	{
		SDL_Renderer* renderer;
		SDL_Texture* texture;
		int w, h;

		canvas(SDL_Renderer* r, int w, int h)
			: renderer(r), w(w), h(h)
		{
			texture = SDL_CreateTexture(
				renderer,
				SDL_PIXELFORMAT_RGBA8888,
				SDL_TEXTUREACCESS_TARGET,
				w, h
			);
		}

		~canvas()
		{
			SDL_DestroyTexture(texture);
		}

		int width() const { return w; }
		int height() const { return h; }

		void clear()
		{
			SDL_SetRenderTarget(renderer, texture);
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
			SDL_SetRenderTarget(renderer, nullptr);
		}
	};

	struct window
	{
		SDL_Window* win{};
		SDL_Renderer* renderer{};

		window()
		{
			SDL_Init(SDL_INIT_VIDEO);

			win = SDL_CreateWindow(
				"Fighter",
				SDL_WINDOWPOS_CENTERED,
				SDL_WINDOWPOS_CENTERED,
				800, 600,
				0
			);

			renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
		}

		~window()
		{
			SDL_DestroyRenderer(renderer);
			SDL_DestroyWindow(win);
			SDL_Quit();
		}

		void present(const canvas& c)
		{
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);

			SDL_RenderCopy(renderer, c.texture, nullptr, nullptr);
			SDL_RenderPresent(renderer);
		}
	};

	void draw_rect(int x, int y, int w, int h) override
	{
		SDL_SetRenderTarget(_canvas.renderer, _canvas.texture);

		SDL_SetRenderDrawColor(_canvas.renderer, 255, 0, 0, 255);
		SDL_Rect r{ x, y, w, h };
		SDL_RenderFillRect(_canvas.renderer, &r);

		SDL_SetRenderTarget(_canvas.renderer, nullptr);
	}

	window _window{};
	canvas _canvas{ _window.renderer, 800, 600 };
};

inline ::std::unique_ptr<graphic::renderer> default_renderer()
{
	return ::std::make_unique<sdl_renderer>();
}