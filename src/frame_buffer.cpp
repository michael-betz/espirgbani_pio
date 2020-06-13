// Implements a framebuffer with N layers and alpha blending

#include <string.h>
#include <stdio.h>
#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "i2s_parallel.h"
#include "rgb_led_panel.h"
#include "common.h"
#include "frame_buffer.h"

// static const char *T = "FRAME_BUFFER";
EventGroupHandle_t layersDoneDrawingFlags = NULL;

// framebuffer with `N_LAYERS` in MSB ABGR LSB format
// Colors are premultiplied with their alpha values for easiser compositing
unsigned g_frameBuff[N_LAYERS][DISPLAY_WIDTH * DISPLAY_HEIGHT];

void initFb()
{
	layersDoneDrawingFlags = xEventGroupCreate();
	// set all layers to transparent black and unblock
	for (int i=0; i<N_LAYERS; i++) {
		xEventGroupSetBits(layersDoneDrawingFlags, (1 << i));
		setAll(i, 0x00000000);
	}
}

// called by layer drawing functions before beginning to draw a frame
// blocks until compositing cycle is complete
void startDrawing(unsigned layer)
{
	// waits on doneUpdating()
	xEventGroupWaitBits(
		layersDoneDrawingFlags,
		(1 << layer),
		pdTRUE,
		pdTRUE,
		500 / portTICK_PERIOD_MS
	);
}

// called by layer drawing functions when done with drawing a frame
void doneDrawing(unsigned layer)
{
	xEventGroupSetBits(layersDoneDrawingFlags, (1 << layer));
}

// called before compositing
void waitDrawingDone()
{
	// waits on doneDrawing() for all layers
	xEventGroupWaitBits(
		layersDoneDrawingFlags,
		(1 << N_LAYERS) - 1,  // N_LAYERS = 3, bits = 1b111
		pdTRUE,
		pdTRUE,
		500 / portTICK_PERIOD_MS
	);
	// All bits cleared: Blocks startDrawing() for all layers
}

// called when done compositing. Completes the frame cycle.
void doneUpdating()
{
	// Set all bits again, unlocking startDrawing() for all layers
	xEventGroupSetBits(layersDoneDrawingFlags, (1 << N_LAYERS) - 1);
}

//Get a blended pixel from the N layers of frameBuffer,
// assuming the image is a DISPLAY_WIDTHx32 8A8R8G8B image. Color values are premultipleid with alpha
// Returns it as an uint32 with the lower 24 bits containing the RGB values.
unsigned getBlendedPixel(unsigned x, unsigned y)
{
	unsigned resR=0, resG=0, resB=0;
	for (unsigned l=0; l<N_LAYERS; l++) {
		// Get a pixel value of one layer
		unsigned p = g_frameBuff[l][x + y * DISPLAY_WIDTH];
		resR = INT_PRELERP(resR, GR(p), GA(p));
		resG = INT_PRELERP(resG, GG(p), GA(p));
		resB = INT_PRELERP(resB, GB(p), GA(p));
	}
	return (resB<<16) | (resG<<8) | resR;
}

// Set a pixel in frmaebuffer at p
void setPixel(unsigned layer, unsigned x, unsigned y, unsigned color) {
	if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT || layer >= N_LAYERS)
		return;
	//(a<<24) | (b<<16) | (g<<8) | r;
	g_frameBuff[layer][x + y * DISPLAY_WIDTH] = color;
}

void setPixelColor(unsigned layer, unsigned x, unsigned y, unsigned cIndex, unsigned color) {
	if (x>=DISPLAY_WIDTH || y>=DISPLAY_HEIGHT || layer>=N_LAYERS)
		return;
	unsigned temp = g_frameBuff[layer][x + y * DISPLAY_WIDTH];
	temp &= 0xFFFFFF00 << (cIndex * 8);
	temp |= color << (cIndex*8);
	g_frameBuff[layer][x + y * DISPLAY_WIDTH] = temp;
}

// Set a pixel in frmaebuffer at p
unsigned getPixel(unsigned layer, unsigned x, unsigned y) {
	if (layer >= N_LAYERS)
		return 0;
	x = MIN(x, DISPLAY_WIDTH - 1);
	y = MIN(y, DISPLAY_HEIGHT - 1);
	// (a<<24) | (b<<16) | (g<<8) | r;
	return g_frameBuff[layer][x + y * DISPLAY_WIDTH];
}

void setPixelOver(unsigned layer, unsigned x, unsigned y, unsigned color) {
	if (x>=DISPLAY_WIDTH || y>=DISPLAY_HEIGHT || layer>=N_LAYERS)
		return;
	unsigned p = g_frameBuff[layer][x + y * DISPLAY_WIDTH];
	unsigned resR = INT_PRELERP(GR(p), GR(color), GA(color));
	unsigned resG = INT_PRELERP(GG(p), GG(color), GA(color));
	unsigned resB = INT_PRELERP(GB(p), GB(color), GA(color));
	unsigned resA = INT_PRELERP(GA(p), GA(color), GA(color));
	g_frameBuff[layer][x + y * DISPLAY_WIDTH] = SRGBA(resR, resG, resB, resA);
}

unsigned fadeOut(unsigned layer, unsigned factor) {
	if (layer >= N_LAYERS)
		return 0;
	if (factor <= 0)
		factor = 1;
	unsigned scale = 255 - factor;
	unsigned *p = (unsigned*)g_frameBuff[layer];
	unsigned nTouched = 0;
	for (int i=0; i<DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
		if (*p > 0){
			*p = scale32(scale, *p);
			nTouched++;
		}
		p++;
	}
	return nTouched;
}

// set all pixels of a layer to a color
void setAll(unsigned layer, unsigned color) {
	if (layer >= N_LAYERS){
		return;
	}
	unsigned *p = (unsigned*)g_frameBuff[layer];
	for (int i=0; i<DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
		*p++ = color;
	}
}

// ---------------
//  Test patterns
// ---------------
static void tp_stripes(unsigned width, unsigned offset, bool isY)
{
	for (unsigned y=0; y<DISPLAY_HEIGHT; y++) {
		for (unsigned x=0; x<DISPLAY_WIDTH; x++) {
			unsigned var = isY ? x : y;
			unsigned col = (var + offset) % width == 0 ? 0xFFFFFFFF : 0xFF000000;
			setPixel(2, x, y, col);
		}
	}
	updateFrame();
}

static void tp_stripes_sequence(bool isY)
{
	for (unsigned i=0; i<8; i++) {
		log_d("stripes %d / 8", i + 1);
		tp_stripes(8, i, isY);
		delay(500);
	}
	for (unsigned i=0; i<4; i++) {
		log_d("stripes %d / 2", (i % 2) + 1);
		tp_stripes(2, i % 2, isY);
		delay(500);
	}
}

void tp_task(void *pvParameters)
{
	while(1) {
		setAll(0, 0xFF000000);
		setAll(1, 0xFF000000);
		setAll(2, 0xFF000000);
		updateFrame();

		log_d("Diagonal");
		for (unsigned y=0; y<DISPLAY_HEIGHT; y++)
			for (unsigned x=0; x<DISPLAY_WIDTH; x++)
				setPixel(2, x, y, (x - y) % DISPLAY_HEIGHT == 0 ? 0xFFFFFFFF : 0xFF000000);
		updateFrame();
		delay(5000);

		log_d("Vertical stripes ...");
		tp_stripes_sequence(true);

		log_d("Horizontal stripes ...");
		tp_stripes_sequence(false);

		log_d("All red");
		setAll(2, 0xFF0000FF);
		updateFrame();
		delay(1000);

		log_d("All green");
		setAll(2, 0xFF00FF00);
		updateFrame();
		delay(1000);

		log_d("All blue");
		setAll(2, 0xFFFF0000);
		updateFrame();
		delay(1000);
	}
}

// take a shade (0-15) from animation file and 24 bit RGB color. Return a RGBA color.
#define GET_ANI_COLOR(p, c) ()

// takes a 4 bit animation pixel, returns the 32 bit RGBA pixel
static unsigned get_pix_color(unsigned pix, unsigned color)
{
	if (pix == 0x0A)  // a transparent pixel
		return 0;
	else
		return scale32(pix * 17, color) | 0xFF000000;
}

void setFromFile(FILE *f, unsigned layer, unsigned color, bool lock_fb)
{
	uint8_t frm_buff[DISPLAY_WIDTH * DISPLAY_HEIGHT / 2], *pix=frm_buff;
	unsigned *p = g_frameBuff[layer];
	fread(frm_buff, 1, sizeof(frm_buff), f);

	if (lock_fb)
		startDrawing(2);
	for (int y=0; y<DISPLAY_HEIGHT; y++) {
		for (int x=0; x<DISPLAY_WIDTH; x+=2) {
			// unpack the 2 pixels per byte data into 1 pixel per byte and set the framebuffer
			*p++ = get_pix_color(*pix >> 4, color);
			*p++ = get_pix_color(*pix & 0x0F, color);
			pix++;
		}
	}
	if (lock_fb)
		doneDrawing(2);
}
