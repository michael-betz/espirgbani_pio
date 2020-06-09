#ifndef FRAME_BUFFER_H
#define FRAME_BUFFER_H
#include <stdio.h>
#include "rgb_led_panel.h"

#define N_LAYERS	3

#define BLEND_LAYERS(t,m,b) ((t * (255 - m) + b * m) / 255)

// Fast integer `scaling` a * b (255 * 255 = 255)
// from http://stereopsis.com/doubleblend.html
#define INT_MULT(a,b,t)        (((a) + 1) * (b) >> 8)
#define INT_PRELERP(p, q, a)   ((p) + (q) - INT_MULT(a, p, 0))

// from http://www.cs.princeton.edu/courses/archive/fall00/cs426/papers/smith95a.pdf
// t = 16 bit temporary variable.
// #define INT_MULT(a,b,t) ((t)=(a)*(b)+0x80, ((((t)>>8)+(t))>>8))
// #define INT_PRELERP(p, q, a, t) ((p) + (q) - INT_MULT(a, p, t))
// B over A (premultipied alpha):  C' = INT_PRELERP(A', B', beta, t)

//Scales all 4 components in p with the same scaling factor scale (255*255=255)
static inline unsigned scale32(unsigned scale, unsigned p) {
	scale += 1;
	unsigned ag = (p>>8) & 0x00FF00FF;
	unsigned rb =  p     & 0x00FF00FF;
	unsigned sag = scale * ag;
	unsigned srb = scale * rb;
	sag =  sag     & 0xFF00FF00;
	srb = (srb>>8) & 0x00FF00FF;
	return sag | srb;
}

#define GC(p,ci) (((p)>>((ci)*8))&0xFF)
#define GR(p)    (GC(p,0))
#define GG(p)    (GC(p,1))
#define GB(p)    (GC(p,2))
#define GA(p)    (GC(p,3))
#define SRGBA(r,g,b,a) (((a)<<24) | ((b)<<16) | ((g)<<8) | (r))

extern unsigned g_frameBuff[N_LAYERS][DISPLAY_WIDTH*DISPLAY_HEIGHT];

unsigned getBlendedPixel(unsigned x, unsigned y);

// SET / GET a single pixel on a layer to a specific RGBA color in the framebuffer
void setPixel(unsigned layer, unsigned x, unsigned y, unsigned color);
void setPixelColor(unsigned layer, unsigned x, unsigned y, unsigned cIndex, unsigned color);
unsigned getPixel(unsigned layer, unsigned x, unsigned y);

// Draw over a pixel in frmaebuffer at p, color must be premultiplied alpha
void setPixelOver(unsigned layer, unsigned x, unsigned y, unsigned color);

// Set whole layer to fixed color
void setAll(unsigned layer, unsigned color);

// blocks a few seconds as it runs through test pattern sequence once
void tp_sequence();

// write image from a runDmd image file into layer with shades of color
void setFromFile(FILE *f, unsigned layer, unsigned color);

// make the layer a little more transparent each call.
// Factor=255 is strongest. Returns the number of pixels changed.
unsigned fadeOut(unsigned layer, unsigned factor);

// initialize all layers with translucent black
void initFb();

// -----------------------
//  layer synchronization
// -----------------------
// the idea is to not write the composite image to the I2S DMA chain before
// all layers have finished updating the current frame.
// vice versa, the layers cannot be changed while compositing.
// uses eventGroupBits:

// called by layer drawing functions before beginning to draw a frame
void startDrawing(unsigned layer);

// called by layer drawing functions when done with drawing a frame
void doneDrawing(unsigned layer);

// called when done compositing. Completes the frame cycle.
void doneUpdating();

// called before compositing
void waitDrawingDone();

#endif
