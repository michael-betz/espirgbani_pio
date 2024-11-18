#ifndef FRAME_BUFFER_H
#define FRAME_BUFFER_H
#include "rgb_led_panel.h"
#include <stdio.h>

#define N_LAYERS 3

#define BLEND_LAYERS(t, m, b) ((t * (255 - m) + b * m) / 255)

// Fast integer `scaling` a * b (255 * 255 = 255)
// from http://stereopsis.com/doubleblend.html
#define INT_MULT(a, b, t) (((a) + 1) * (b) >> 8)
#define INT_PRELERP(p, q, a) ((p) + (q)-INT_MULT(a, p, 0))

// from
// http://www.cs.princeton.edu/courses/archive/fall00/cs426/papers/smith95a.pdf
// t = 16 bit temporary variable.
// #define INT_MULT(a,b,t) ((t)=(a)*(b)+0x80, ((((t)>>8)+(t))>>8))
// #define INT_PRELERP(p, q, a, t) ((p) + (q) - INT_MULT(a, p, t))
// B over A (premultipied alpha):  C' = INT_PRELERP(A', B', beta, t)

// Scales all 4 components in p with the same scaling factor scale (255*255=255)
static inline unsigned scale32(unsigned scale, unsigned p) {
	scale += 1;
	unsigned ag = (p >> 8) & 0x00FF00FF;
	unsigned rb = p & 0x00FF00FF;
	unsigned sag = scale * ag;
	unsigned srb = scale * rb;
	sag = sag & 0xFF00FF00;
	srb = (srb >> 8) & 0x00FF00FF;
	return sag | srb;
}

#define GC(p, ci) (((p) >> ((ci)*8)) & 0xFF)
#define GR(p) (GC(p, 0))
#define GG(p) (GC(p, 1))
#define GB(p) (GC(p, 2))
#define GA(p) (GC(p, 3))
#define SRGBA(r, g, b, a) (((a) << 24) | ((b) << 16) | ((g) << 8) | (r))

// shades of the color in set_shade_*
#define N_SHADES 16 // not really changeable

extern unsigned g_frameBuff[N_LAYERS][DISPLAY_WIDTH * DISPLAY_HEIGHT];

unsigned getBlendedPixel(unsigned x, unsigned y);

// SET / GET a single pixel on a layer to a specific RGBA color in the
// framebuffer
void setPixel(unsigned layer, unsigned x, unsigned y, unsigned color);
void setPixelColor(
	unsigned layer, unsigned x, unsigned y, unsigned cIndex, unsigned color
);
unsigned getPixel(unsigned layer, unsigned x, unsigned y);

// pre-calculate a palette of 16 shades fading up from opaque black
void set_shade_opaque(unsigned color, unsigned *shades);

// pre-calculate a palette of 16 shades fading up from transparent
void set_shade_transparent(unsigned color, unsigned *shades);

// get opaque shades of a specific hue (0 .. HSV_HUE_MAX). Gamma corrected!
void set_shade_h(uint16_t hue, unsigned *shades);

// get transparent shades of a specific hue (0 .. HSV_HUE_MAX)
void set_shade_ht(uint16_t hue, unsigned *shades);

// Xialoin Wu antialiased line drawer, use set_shade_* to set color
// uses setPixelOver to mix colors, not the fastes but looks fabulous!
void aaLine(unsigned layer, unsigned *shades, int X0, int Y0, int X1, int Y1);

// using float, from https://en.wikipedia.org/wiki/Xiaolin_Wu's_line_algorithm
void aaLine2(
	unsigned layer, unsigned *shades, float x0, float y0, float x1, float y1
);

// Draw over a pixel in frmaebuffer at p, color must be premultiplied alpha
void setPixelOver(unsigned layer, unsigned x, unsigned y, unsigned color);

// Set whole layer to fixed color
void setAll(unsigned layer, unsigned color);

// blocks and displays test-patterns forever
void tp_task(void *pvParameters);

// write image from a runDmd image file into layer with shades of color
void setFromFile(FILE *f, unsigned layer, unsigned color);

// make the layer a little more transparent each call.
// Factor=255 is strongest. Returns the number of pixels changed.
unsigned fadeOut(unsigned layer, unsigned factor);

// initialize all layers with translucent black
void initFb();

#endif
