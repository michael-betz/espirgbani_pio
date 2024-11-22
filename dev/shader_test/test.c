// demo the drawing engine on a PC using SDL2
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <SDL2/SDL.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <shaders.h>
#include <frame_buffer.h>
#include "common.h"

#define D_SCALE 4.0

SDL_Renderer *rr = NULL;
SDL_Window* window = NULL;

static void init_sdl()
{
	srand(time(NULL));

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "could not initialize sdl2: %s\n", SDL_GetError());
		return;
	}

	if (SDL_CreateWindowAndRenderer(
		DISPLAY_WIDTH, DISPLAY_HEIGHT, SDL_WINDOW_RESIZABLE, &window, &rr
	)) {
		fprintf(stderr, "could not create window: %s\n", SDL_GetError());
		return;
	}

	SDL_RenderSetScale(rr, D_SCALE, D_SCALE);
	SDL_SetRenderDrawBlendMode(rr, SDL_BLENDMODE_ADD);
}

bool is_running = true;

void one_iter()
{
	static unsigned frm = 0;
	static int current_shader = 0;

	SDL_Event e;

	while (SDL_PollEvent(&e)) {
		switch (e.type) {
			case SDL_QUIT:
				is_running = false;
				#ifdef __EMSCRIPTEN__
					emscripten_cancel_main_loop();
				#endif
				break;
			case SDL_KEYDOWN:
				switch(e.key.keysym.sym) {
					case SDLK_LEFT:
						current_shader--;
						if (current_shader < 0)
							current_shader = 0;
						break;

					case SDLK_RIGHT:
						current_shader++;
						if (current_shader > 1)
							current_shader = 1;
						break;
				}
				break;
		}
	}

	drawXorFrame(frm);

	SDL_SetRenderDrawColor(rr, 0x00, 0x00, 0x00, 0xFF);
	SDL_RenderClear(rr);

	for (int y=0; y<DISPLAY_HEIGHT; y++) {
		for (int x=0; x<DISPLAY_WIDTH; x++) {
			unsigned c = getBlendedPixel(x, y);
			SDL_SetRenderDrawColor(rr, GR(c), GG(c), GB(c), 0xFF);
	        SDL_RenderDrawPoint(rr, x, y);
		}
	}
	SDL_RenderPresent(rr);

	frm++;
}


int main(int argc, char* args[])
{
	init_sdl();

	#ifdef __EMSCRIPTEN__
		emscripten_set_main_loop(one_iter, 0, 1);
	#else
		while (is_running) {
			one_iter();
			SDL_Delay(20);
		}

		SDL_DestroyRenderer(rr);
		SDL_DestroyWindow(window);
		SDL_Quit();
	#endif

	return 0;
}
