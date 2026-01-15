#include <stdexec/execution.hpp>

#include "./fighter.h"

#undef main
int main()
{
	::stdexec::sync_wait(fighter_scene());
	return 0;
}
// #include <SDL2/SDL.h>
// #include <cstdio>
// #include <vector>
// #include <chrono>
// 
// #include "stb_image.h"
// #undef main
// 
// int main(int argc, char** argv) {
// 	if (argc < 2) {
// 		printf("Usage: %s xxx.gif\n", argv[0]);
// 		return -1;
// 	}
// 
// 	/* ---------- 读文件到内存 ---------- */
// 	FILE* f = fopen(argv[1], "rb");
// 	if (!f) {
// 		perror("fopen");
// 		return -1;
// 	}
// 	fseek(f, 0, SEEK_END);
// 	long size = ftell(f);
// 	rewind(f);
// 
// 	std::vector<unsigned char> fileData(size);
// 	fread(fileData.data(), 1, size, f);
// 	fclose(f);
// 
// 	/* ---------- 解码 GIF ---------- */
// 	int* delays = nullptr;
// 	int width, height, frames;
// 	unsigned char* pixels = stbi_load_gif_from_memory(
// 		fileData.data(), size,
// 		&delays,
// 		&width, &height,
// 		&frames,
// 		nullptr, 4
// 	);
// 
// 	if (!pixels) {
// 		printf("Failed to load gif\n");
// 		return -1;
// 	}
// 
// 	printf("GIF: %dx%d, frames=%d\n", width, height, frames);
// 
// 	/* ---------- SDL init ---------- */
// 	SDL_Init(SDL_INIT_VIDEO);
// 
// 	SDL_Window* window = SDL_CreateWindow(
// 		"GIF Player",
// 		SDL_WINDOWPOS_CENTERED,
// 		SDL_WINDOWPOS_CENTERED,
// 		width, height,
// 		SDL_WINDOW_SHOWN
// 	);
// 
// 	SDL_Renderer* renderer = SDL_CreateRenderer(
// 		window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
// 	);
// 
// 	/* ---------- 每帧建 texture ---------- */
// 	std::vector<SDL_Texture*> textures;
// 	for (int i = 0; i < frames; i++) {
// 		SDL_Texture* tex = SDL_CreateTexture(
// 			renderer,
// 			SDL_PIXELFORMAT_RGBA32,
// 			SDL_TEXTUREACCESS_STATIC,
// 			width, height
// 		);
// 
// 		SDL_UpdateTexture(
// 			tex,
// 			nullptr,
// 			pixels + i * width * height * 4,
// 			width * 4
// 		);
// 
// 		textures.push_back(tex);
// 	}
// 
// 	/* ---------- 播放循环 ---------- */
// 	bool running = true;
// 	SDL_Event e;
// 	int frame = 0;
// 
// 	while (running) {
// 		auto start = std::chrono::high_resolution_clock::now();
// 
// 		while (SDL_PollEvent(&e)) {
// 			if (e.type == SDL_QUIT)
// 				running = false;
// 		}
// 
// 		SDL_RenderClear(renderer);
// 		SDL_RenderCopy(renderer, textures[frame], nullptr, nullptr);
// 		SDL_RenderPresent(renderer);
// 
// 		int delay = delays ? delays[frame] : 100;
// 		if (delay <= 0) delay = 100;
// 
// 		frame = (frame + 1) % frames;
// 
// 		SDL_Delay(delay);
// 	}
// 
// 	/* ---------- cleanup ---------- */
// 	for (auto t : textures)
// 		SDL_DestroyTexture(t);
// 
// 	stbi_image_free(pixels);
// 	if (delays) stbi_image_free(delays);
// 
// 	SDL_DestroyRenderer(renderer);
// 	SDL_DestroyWindow(window);
// 	SDL_Quit();
// 
// 	return 0;
// }
