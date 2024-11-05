#include "rgb_led_panel.h"
#include "assert.h"
#include "common.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "frame_buffer.h"
#include "i2s_parallel.h"
#include "json_settings.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// bits / color, larger values = more sub-frames and more flicker, max: 8
#define BITPLANE_CNT 7

// x * DISPLAY_HEIGHT RGB leds, 2 pixels per 16-bit value...
#define BITPLANE_SZ (DISPLAY_WIDTH * DISPLAY_HEIGHT / 2) // [16 bit words]

// -------------------------------------------
//  Meaning of the bits in a 16 bit DMA word
// -------------------------------------------
// Upper half RGB
#define BIT_R1 (1 << 0)
#define BIT_G1 (1 << 1)
#define BIT_B1 (1 << 2)
// Lower half RGB
#define BIT_R2 (1 << 3)
#define BIT_G2 (1 << 4)
#define BIT_B2 (1 << 5)
#define BIT_A (1 << 6)
#define BIT_B (1 << 7)
#define BIT_C (1 << 8)
#define BIT_D (1 << 9)
#define BIT_LAT (1 << 10)
#define BIT_BLANK (1 << 11)
// -1 = don't care

static const char *T = "LED_PANEL";

unsigned g_frames = 0; // frame counter
unsigned g_f_del = 33; // delay between frames [ms]

// Double buffering has been removed to save RAM. No visual differences!
uint16_t *bitplane[BITPLANE_CNT];
// DISPLAY_WIDTH * 32 * 3 array with image data, 8R8G8B

// .json configurable parameters
static int latch_offset = 0;
static unsigned extra_blank = 0;
static bool isColumnSwapped = false;
static int ledBrightness = 5;

void set_brightness(int value)
{
	if (value < 0)
		value = 0;

	if (value > 120)
		value = 120;

	ledBrightness = value;
}

void init_rgb() {
	// PD_BAD GPIO. If this is high we don't have juice. Run in low power mode
	gpio_set_direction(GPIO_PD_BAD, GPIO_MODE_INPUT);
	gpio_set_pull_mode(GPIO_PD_BAD, GPIO_PULLUP_ONLY);  // open drain output

	initFb();

	i2s_parallel_buffer_desc_t bufdesc[1 << BITPLANE_CNT];
	i2s_parallel_config_t cfg;

	cfg.gpio_clk = GPIO_CLK;
	cfg.gpio_bus[0] = GPIO_R1;
	cfg.gpio_bus[1] = GPIO_G1;
	cfg.gpio_bus[2] = GPIO_B1;
	cfg.gpio_bus[3] = GPIO_R2;
	cfg.gpio_bus[4] = GPIO_G2;
	cfg.gpio_bus[5] = GPIO_B2;
	cfg.gpio_bus[6] = GPIO_A;
	cfg.gpio_bus[7] = GPIO_B;
	cfg.gpio_bus[8] = GPIO_C;
	cfg.gpio_bus[9] = GPIO_D;
	cfg.gpio_bus[10] = GPIO_LAT;
	cfg.gpio_bus[11] = GPIO_BLANK;
	cfg.gpio_bus[12] = (gpio_num_t)(-1);
	cfg.gpio_bus[13] = (gpio_num_t)(-1);
	cfg.gpio_bus[14] = (gpio_num_t)(-1);
	cfg.gpio_bus[15] = (gpio_num_t)(-1);

	cfg.bits = I2S_PARALLEL_BITS_16, cfg.bufa = bufdesc;
	cfg.bufb = bufdesc;

	//--------------------------
	// .json configuration
	//--------------------------
	// get `panel` dictionary
	cJSON *jPanel = jGet(getSettings(), "panel");

	// delay between updateFrame calls [ms]
	g_f_del = 1000 / jGetI(jPanel, "max_frame_rate", 30);

	// Swap pixel x[0] with x[1]
	isColumnSwapped = jGetB(jPanel, "column_swap", false);
	if (isColumnSwapped)
		ESP_LOGV(T, "column_swap applied!");

	// adjust clock cycle of the latch pulse (nominally = 0 = last pixel)
	latch_offset = (DISPLAY_WIDTH - 1) + jGetI(jPanel, "latch_offset", 0);
	latch_offset %= DISPLAY_WIDTH;
	ESP_LOGV(T, "latch_offset = %d", latch_offset);

	// adjust extra blanking cycles to reduce ghosting effects
	extra_blank = jGetI(jPanel, "extra_blank", 1);

	// set clock divider
	cfg.clk_div = jGetI(jPanel, "clkm_div_num", 4);
	if (cfg.clk_div < 1)
		cfg.clk_div = 1;
	ESP_LOGV(T, "clkm_div_num = %d", cfg.clk_div);

	cfg.is_clk_inverted = jGetB(jPanel, "is_clk_inverted", true);

	//--------------------------
	// init the sub-frames
	//--------------------------
	for (int i = 0; i < BITPLANE_CNT; i++) {
		bitplane[i] =
			(uint16_t *)heap_caps_malloc(BITPLANE_SZ * 2, MALLOC_CAP_DMA);
		assert(bitplane[i] && "Can't allocate bitplane memory");
		memset(bitplane[i], 0, BITPLANE_SZ * 2);
	}

	// Do binary time division setup. Essentially, we need n of plane 0, 2n of
	// plane 1, 4n of plane 2 etc, but that needs to be divided evenly over time
	// to stop flicker from happening. This little bit of code tries to do that
	// more-or-less elegantly.
	int times[BITPLANE_CNT] = {0};
	for (int i = 0; i < ((1 << BITPLANE_CNT) - 1); i++) {
		int ch = 0;

		// Find plane that needs insertion the most
		for (int j = 0; j < BITPLANE_CNT; j++) {
			if (times[j] <= times[ch])
				ch = j;
		}

		// Insert the plane
		bufdesc[i].memory = bitplane[ch];
		bufdesc[i].size = BITPLANE_SZ * 2;

		// Magic to make sure we choose this bitplane an appropriate time later
		// next time
		times[ch] += (1 << (BITPLANE_CNT - ch));
	}

	// End markers
	bufdesc[((1 << BITPLANE_CNT) - 1)].memory = NULL;

	// Setup I2S
	i2s_parallel_setup(&I2S1, &cfg);

	ESP_LOGI(T, "I2S setup done.");
}

void updateFrame() {
	// Wait until all the layers are done updating
	waitDrawingDone();

	// Check if we need to be in low power mode
	int ledBrightness_ = ledBrightness;
	if (gpio_get_level(GPIO_PD_BAD) && ledBrightness_ > 5)
		ledBrightness_ = 5;

	for (unsigned int y = 0; y < 16; y++) {
		// Precalculate line bits of the *previous* line, which is the one we're
		// displaying now
		int lbits = 0;

		if ((y - 1) & 1)
			lbits |= BIT_A;
		if ((y - 1) & 2)
			lbits |= BIT_B;
		if ((y - 1) & 4)
			lbits |= BIT_C;
		if ((y - 1) & 8)
			lbits |= BIT_D;

		for (int fx = 0; fx < DISPLAY_WIDTH; fx++) {
			int x = fx;

			if (isColumnSwapped)
				x ^= 1;

			int v = lbits;

			// Do not show image while the line bits are changing
			if (fx < extra_blank || fx >= ledBrightness_)
				v |= BIT_BLANK;

			// latch on last bit...
			if (fx == latch_offset)
				v |= BIT_LAT;

			// Does alpha blending of all graphical layers, a rather
			// expensive operation and best kept out of innermost loop.
			int c1 = getBlendedPixel(x, y);
			int c2 = getBlendedPixel(x, y + 16);

			for (int pl = 0; pl < BITPLANE_CNT; pl++) {
				// reset RGB bits
				int v_ = v;

				// bitmask for pixel data in input for this bitplane
				int mask = (1 << (8 - BITPLANE_CNT + pl));

				if (c1 & (mask << 16))
					v_ |= BIT_R1;
				if (c1 & (mask << 8))
					v_ |= BIT_G1;
				if (c1 & (mask << 0))
					v_ |= BIT_B1;
				if (c2 & (mask << 16))
					v_ |= BIT_R2;
				if (c2 & (mask << 8))
					v_ |= BIT_G2;
				if (c2 & (mask << 0))
					v_ |= BIT_B2;

				// Save the calculated value to the bitplane memory
				bitplane[pl][y * DISPLAY_WIDTH + x] = v_;
			}
		}
	}

	doneUpdating();
	g_frames++;
}
