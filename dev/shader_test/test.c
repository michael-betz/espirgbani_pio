// demo the drawing engine on a PC using SDL2
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_surface.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <shaders.h>
#include <frame_buffer.h>
#include "common.h"

#define D_SCALE 6.0

void (*shader_fcts[]) (unsigned frm) = {
	drawXorFrame,
	drawBendyFrame,
	drawAlienFlameFrame,
	drawDoomFlameFrame,
	drawLasers,
};
#define N_SHADERS (sizeof(shader_fcts) / sizeof(shader_fcts[0]))

SDL_Renderer *rr = NULL;
SDL_Window* window = NULL;
// SDL_Surface* surf = NULL;
// SDL_Rect dst_rect = {
// 	.x = 0, .y = 0, .w = DISPLAY_WIDTH, .h = DISPLAY_HEIGHT
// };

static void init_sdl()
{
	srand(time(NULL));

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "could not initialize sdl2: %s\n", SDL_GetError());
		return;
	}

	if (SDL_CreateWindowAndRenderer(
		DISPLAY_WIDTH * D_SCALE,
		DISPLAY_HEIGHT * D_SCALE,
		SDL_WINDOW_RESIZABLE,
		&window,
		&rr
	)) {
		fprintf(stderr, "could not create window: %s\n", SDL_GetError());
		return;
	}

	SDL_RenderSetScale(rr, D_SCALE, D_SCALE);
	SDL_SetRenderDrawBlendMode(rr, SDL_BLENDMODE_NONE);

	// surf = SDL_CreateRGBSurfaceFrom(
	// 	g_frameBuff,
	// 	DISPLAY_WIDTH,
	// 	DISPLAY_HEIGHT,
	// 	32,
	// 	DISPLAY_WIDTH * 4,
	// 	0x000000FF,
	// 	0x0000FF00,
	// 	0x00FF0000,
	// 	0xFF000000
	// );
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
						if (current_shader >= N_SHADERS)
							current_shader = N_SHADERS - 1;
						break;
				}
				break;
		}
	}

	shader_fcts[current_shader](frm);

	// SDL_Texture *tex = SDL_CreateTextureFromSurface(rr, surf);
	// SDL_RenderCopy(rr, tex, NULL, &dst_rect);

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

	SDL_SetRenderDrawColor(rr, 0x22, 0x22, 0x22, 0xFF);
	SDL_RenderClear(rr);

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
