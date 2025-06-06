// Implements a framebuffer with N layers and alpha blending

#include "frame_buffer.h"
#include "fast_hsv2rgb.h"
#include "rgb_led_panel.h"
#include "val2pwm.h"
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <esp_log.h>

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2s_parallel.h"
#include "json_settings.h"

SemaphoreHandle_t fbSemaphore = NULL;
#endif

static const char *T = "FRAME_BUFFER";

static bool is_gamma = false;
static bool is_locked = false;

// framebuffer with `N_LAYERS` in MSB ABGR LSB format
// Colors are premultiplied with their alpha values for easiser compositing
unsigned g_frameBuff[N_LAYERS][DISPLAY_WIDTH * DISPLAY_HEIGHT];

#if defined(ESP_PLATFORM)
void lockFrameBuffer() {
	if (is_locked)
		xSemaphoreTake(fbSemaphore, portTICK_PERIOD_MS * 500);
}

void releaseFrameBuffer() { xSemaphoreGive(fbSemaphore); }

void initFb() {
	fbSemaphore = xSemaphoreCreateBinary();

	// set all layers to transparent
	for (int i = 0; i < N_LAYERS; i++)
		setAll(i, 0);

	cJSON *jPanel = jGet(getSettings(), "panel");
	is_gamma = jGetB(jPanel, "is_gamma", true);
	is_locked = jGetB(jPanel, "is_locked", true);

	xSemaphoreGive(fbSemaphore);
}
#endif

// Get a blended pixel from the N layers of frameBuffer,
//  assuming the image is a DISPLAY_WIDTHx32 8A8R8G8B image. Color values are
//  premultipleid with alpha Returns it as an uint32 with the lower 24 bits
//  containing the RGB values.
unsigned getBlendedPixel(unsigned x, unsigned y) {
	unsigned resR = 0, resG = 0, resB = 0;
	for (unsigned l = 0; l < N_LAYERS; l++) {
		// Get a pixel value of one layer
		unsigned p = g_frameBuff[l][x + y * DISPLAY_WIDTH];
		resR = INT_PRELERP(resR, GR(p), GA(p));
		resG = INT_PRELERP(resG, GG(p), GA(p));
		resB = INT_PRELERP(resB, GB(p), GA(p));
	}
	// not sure if worth it ...
	if (is_gamma) {
		resR = valToPwm(resR);
		resG = valToPwm(resG);
		resB = valToPwm(resB);
	}
	return (resB << 16) | (resG << 8) | resR;
}

// Set a pixel in framebuffer at p
void setPixel(unsigned layer, unsigned x, unsigned y, unsigned color) {
	// screen clipping needed for aaLine
	if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT)
		return;
	//(a<<24) | (b<<16) | (g<<8) | r;
	g_frameBuff[layer][x + y * DISPLAY_WIDTH] = color;
}

// This ones's used for the noisy shader. Not sure anymore what it does :p
void setPixelColor(
	unsigned layer, unsigned x, unsigned y, unsigned cIndex, unsigned color
) {
	x &= DISPLAY_WIDTH - 1;
	y &= DISPLAY_HEIGHT - 1;
	unsigned temp = g_frameBuff[layer][x + y * DISPLAY_WIDTH];
	temp &= 0xFFFFFF00 << (cIndex * 8);
	temp |= color << (cIndex * 8);
	g_frameBuff[layer][x + y * DISPLAY_WIDTH] = temp;
}

// Set a pixel in frmaebuffer at p
unsigned getPixel(unsigned layer, unsigned x, unsigned y) {
	x &= DISPLAY_WIDTH - 1;
	y &= DISPLAY_HEIGHT - 1;
	// (a<<24) | (b<<16) | (g<<8) | r;
	return g_frameBuff[layer][x + y * DISPLAY_WIDTH];
}

// used by font drawing function
void setPixelOver(unsigned layer, unsigned x, unsigned y, unsigned color) {
	if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
		// The shader with the rays trigger this with 30 Hz
		// ESP_LOGE(T, "setPixelOver(%d, %d)", x, y);
		return;
	}
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
	unsigned *p = (unsigned *)g_frameBuff[layer];
	unsigned nTouched = 0;
	for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
		if (*p > 0) {
			*p = scale32(scale, *p);
			nTouched++;
		}
		p++;
	}
	return nTouched;
}

// set all pixels of a layer to a color
void setAll(unsigned layer, unsigned color) {
	if (layer >= N_LAYERS) {
		return;
	}
	unsigned *p = (unsigned *)g_frameBuff[layer];
	for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
		*p++ = color;
	}
}

// shift a layer by N rows up
void shiftUp(unsigned layer, unsigned n_rows) {
	if (layer >= N_LAYERS || n_rows <= 0 || n_rows >= DISPLAY_HEIGHT) {
		return;
	}

	// We need to move a block of size `keep_size` from the bottom to the top
	int keep_size = DISPLAY_WIDTH * (DISPLAY_HEIGHT - n_rows);
	int blank_size = DISPLAY_WIDTH * n_rows;
	unsigned *p_top = (unsigned *)g_frameBuff[layer];
	unsigned *p_bottom = p_top + DISPLAY_WIDTH * DISPLAY_HEIGHT;
	// unsigned *p_bottom2 = p_top + DISPLAY_WIDTH * DISPLAY_HEIGHT - n_rows;
	memcpy(p_top, p_bottom - keep_size, keep_size * 4);
	memset(p_bottom - blank_size, 0, blank_size * 4);
}

// get opaque shades of a specific hue (0 .. HSV_HUE_MAX). Gamma corrected!
void set_shade_h(uint16_t hue, unsigned *shades) {
	uint8_t r, g, b;
	for (unsigned i = 0; i < N_SHADES; i++) {
		fast_hsv2rgb_32bit(
			hue, HSV_SAT_MAX, i * HSV_VAL_MAX / (N_SHADES - 1), &r, &g, &b
		);
		shades[i] = SRGBA(r, g, b, 0xFF);
	}
}

// get transparent shades of a specific hue (0 .. HSV_HUE_MAX)
void set_shade_ht(uint16_t hue, unsigned *shades) {
	uint8_t r, g, b;
	fast_hsv2rgb_32bit(hue, HSV_SAT_MAX, HSV_VAL_MAX, &r, &g, &b);
	unsigned temp = SRGBA(r, g, b, 0xFF);
	for (unsigned i = 0; i < N_SHADES; i++) {
		shades[i] = scale32(i * 17, temp);
	}
}

// pre-calculate a palette of N_SHADES shades fading up from opaque black
void set_shade_opaque(unsigned color, unsigned *shades) {
	for (unsigned i = 0; i < N_SHADES; i++)
		shades[i] = scale32(i * 17, color) | 0xFF000000;
}

// pre-calculate a palette of N_SHADES shades fading up from transparent
void set_shade_transparent(unsigned color, unsigned *shades) {
	for (unsigned i = 0; i < N_SHADES; i++)
		shades[i] = scale32(i * 17, color);
}

// takes a 4 bit shade from pinball animation, returns the 32 bit RGBA pixel
static unsigned get_pix_color(unsigned pix, unsigned *shades) {
	pix &= 0x0F;
	if (pix == 0x0A) // a transparent pixel
		return 0;
	return shades[pix];
}

void setFromFile(FILE *f, unsigned layer, unsigned color) {
	uint8_t frm_buff[DISPLAY_WIDTH * DISPLAY_HEIGHT / 2], *pix = frm_buff;
	unsigned *p = g_frameBuff[layer];
	unsigned ret = fread(frm_buff, 1, sizeof(frm_buff), f);
	if (ret != sizeof(frm_buff)) {
		ESP_LOGE(T, "fread error: %d vs %d", ret, sizeof(frm_buff));
		return;
	}

	unsigned shades[N_SHADES];
	set_shade_opaque(color, shades);

	for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT / 2; i++) {
		// unpack the 2 pixels per byte, put their shades in the framebuffer
		*p++ = get_pix_color(*pix >> 4, shades);
		*p++ = get_pix_color(*pix, shades);
		pix++;
	}
}

// Xiaolin Wu antialiased line drawer. Integer optimized.
// (X0,Y0),(X1,Y1) = line to draw
// *shades points to an array of 16 color shades, last entry is the strongest
// use set_shade_*() to generate shades[]
void aaLine(unsigned layer, unsigned *shades, int X0, int Y0, int X1, int Y1) {
	unsigned ErrorAdj, ErrorAcc;
	unsigned ErrorAccTemp, Weighting;
	int DeltaX, DeltaY, Temp, XDir;

	// Make sure the line runs top to bottom
	if (Y0 > Y1) {
		Temp = Y0;
		Y0 = Y1;
		Y1 = Temp;
		Temp = X0;
		X0 = X1;
		X1 = Temp;
	}

	// Draw the initial pixel, which is always exactly intersected by
	// the line and so needs no weighting
	setPixelOver(layer, X0, Y0, shades[N_SHADES - 1]);

	if ((DeltaX = X1 - X0) >= 0) {
		XDir = 1;
	} else {
		XDir = -1;
		DeltaX = -DeltaX; // make DeltaX positive
	}

	// Special-case horizontal, vertical, and diagonal lines, which
	// require no weighting because they go right through the center of
	// every pixel
	if ((DeltaY = Y1 - Y0) == 0) {
		// Horizontal line
		while (DeltaX-- != 0) {
			X0 += XDir;
			setPixelOver(layer, X0, Y0, shades[N_SHADES - 1]);
		}
		return;
	}
	if (DeltaX == 0) {
		// Vertical line
		do {
			Y0++;
			setPixelOver(layer, X0, Y0, shades[N_SHADES - 1]);
		} while (--DeltaY != 0);
		return;
	}
	if (DeltaX == DeltaY) {
		// Diagonal line
		do {
			X0 += XDir;
			Y0++;
			setPixelOver(layer, X0, Y0, shades[N_SHADES - 1]);
		} while (--DeltaY != 0);
		return;
	}

	// line is not horizontal, diagonal, or vertical
	ErrorAcc = 0; // initialize the line error accumulator to 0

	// Is this an X-major or Y-major line?
	if (DeltaY > DeltaX) {
		// Y-major line; calculate 16-bit fixed-point fractional part of a
		// pixel that X advances each time Y advances 1 pixel, truncating the
		// result so that we won't overrun the endpoint along the X axis
		ErrorAdj = ((uint64_t)DeltaX << 32) / DeltaY;
		// Draw all pixels other than the first and last
		while (--DeltaY) {
			ErrorAccTemp = ErrorAcc; // remember current accumulated error
			ErrorAcc += ErrorAdj;	 // calculate error for next pixel
			if (ErrorAcc <= ErrorAccTemp) {
				// The error accumulator turned over, so advance the X coord
				X0 += XDir;
			}
			Y0++; // Y-major, so always advance Y
			// The 4 most significant bits of ErrorAcc give us
			// the intensity weighting for this pixel, and the complement of
			// the weighting for the paired pixel
			Weighting = ErrorAcc >> 28;
			setPixelOver(layer, X0, Y0, shades[Weighting ^ (N_SHADES - 1)]);
			setPixelOver(layer, X0 + XDir, Y0, shades[Weighting]);
		}
		// Draw the final pixel, which is always exactly intersected by the
		// line and so needs no weighting
		setPixelOver(layer, X1, Y1, shades[N_SHADES - 1]);
		return;
	}

	// It's an X-major line; calculate 16-bit fixed-point fractional part
	// of a pixel that Y advances each time X advances 1 pixel, truncating
	// the result to avoid overrunning the endpoint along the X axis
	ErrorAdj = ((uint64_t)DeltaY << 32) / DeltaX;
	// Draw all pixels other than the first and last
	while (--DeltaX) {
		ErrorAccTemp = ErrorAcc; // remember currrent accumulated error
		ErrorAcc += ErrorAdj;	 // calculate error for next pixel
		if (ErrorAcc <= ErrorAccTemp) {
			// The error accumulator turned over, so advance the Y coord
			Y0++;
		}
		X0 += XDir; // X-major, so always advance X
		// The IntensityBits most significant bits of ErrorAcc give us the
		// intensity weighting for this pixel, and the complement of the
		// weighting for the paired pixel
		Weighting = ErrorAcc >> 28;

		setPixelOver(layer, X0, Y0, shades[Weighting ^ (N_SHADES - 1)]);
		setPixelOver(layer, X0, Y0 + 1, shades[Weighting]);
	}
	// Draw the final pixel, which is always exactly intersected by the
	// line and so needs no weighting
	setPixelOver(layer, X1, Y1, shades[N_SHADES - 1]);
}

void swap(float *a, float *b) {
	float c = *a;
	*a = *b;
	*b = c;
}

// fractional part of x
static float fpart(float x) { return (x - floor(x)); }
static float rfpart(float x) { return (ceil(x) - x); }

// plot the pixel at (x, y) with brightness c (where 0 ≤ c ≤ 1)
#define plot(x, y, c)                                                          \
	setPixelOver(layer, x, y, shades[(int)((N_SHADES - 1) * c)])
// #define plot(x, y, c) printf("%3d, %3d, %.3f\n", x, y, c)

// using float, from https://en.wikipedia.org/wiki/Xiaolin_Wu's_line_algorithm
void aaLine2(
	unsigned layer, unsigned *shades, float x0, float y0, float x1, float y1
) {
	bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);
	if (steep) {
		swap(&x0, &y0);
		swap(&x1, &y1);
	}
	if (x0 > x1) {
		swap(&x0, &x1);
		swap(&y0, &y1);
	}

	float dx = x1 - x0;
	float dy = y1 - y0;
	float gradient;
	if (dx == 0.0)
		gradient = 1.0;
	else
		gradient = dy / dx;

	// handle first endpoint
	float xend = round(x0);
	float yend = y0 + gradient * (xend - x0);
	float xgap = rfpart(x0 + 0.5);
	int xpxl1 = xend; // this will be used in the main loop
	int ypxl1 = floor(yend);
	if (steep) {
		plot(ypxl1, xpxl1, rfpart(yend) * xgap);
		plot(ypxl1 + 1, xpxl1, fpart(yend) * xgap);
	} else {
		plot(xpxl1, ypxl1, rfpart(yend) * xgap);
		plot(xpxl1, ypxl1 + 1, fpart(yend) * xgap);
	}
	// first y-intersection for the main loop
	float intery = yend + gradient;

	// handle second endpoint
	xend = round(x1);
	yend = y1 + gradient * (xend - x1);
	xgap = fpart(x1 + 0.5);
	int xpxl2 = xend; // this will be used in the main loop
	int ypxl2 = floor(yend);
	if (steep) {
		plot(ypxl2, xpxl2, rfpart(yend) * xgap);
		plot(ypxl2 + 1, xpxl2, fpart(yend) * xgap);
	} else {
		plot(xpxl2, ypxl2, rfpart(yend) * xgap);
		plot(xpxl2, ypxl2 + 1, fpart(yend) * xgap);
	}

	// main loop
	if (steep) {
		for (int x = xpxl1 + 1; x <= xpxl2 - 1; x++) {
			plot(floor(intery), x, rfpart(intery));
			plot(floor(intery) + 1, x, fpart(intery));
			intery = intery + gradient;
		}
	} else {
		for (int x = xpxl1 + 1; x <= xpxl2 - 1; x++) {
			plot(x, floor(intery), rfpart(intery));
			plot(x, floor(intery) + 1, fpart(intery));
			intery = intery + gradient;
		}
	}
}
