#include "rgb_led_panel.h"
#include "assert.h"
#include "common.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "frame_buffer.h"
#include "i2s_parallel.h"
#include "json_settings.h"

#include "esp_private/periph_ctrl.h"
#include "rom/gpio.h"
#include "rom/lldesc.h"
#include "soc/gpio_periph.h"
#include "soc/i2s_reg.h"
#include "soc/i2s_struct.h"
#include "soc/io_mux_reg.h"

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
// Row address
#define BIT_A (1 << 6)
#define BIT_B (1 << 7)
#define BIT_C (1 << 8)
#define BIT_D (1 << 9)
#define BIT_E (1 << 10)
// Control
#define BIT_LAT (1 << 11)
#define BIT_OE_N (1 << 12)
// -1 = don't care

// 16 bit parallel mode - Save the calculated value to the bitplane memory
// in reverse order to account for I2S Tx FIFO mode1 ordering
#define ESP32_TX_FIFO_POSITION_ADJUST(x) (((x)&1U) ? (x - 1) : (x + 1))

static const char *T = "LED_PANEL";

unsigned g_frames = 0; // frame counter
unsigned g_f_del = 33; // delay between frames [ms]

// Double buffering has been removed to save RAM. No visual differences!
uint16_t *bitplane[BITPLANE_CNT] = {0};
// DISPLAY_WIDTH * 32 * 3 array with image data, 8R8G8B

// .json configurable parameters
static int ledBrightness = 0;
static int low_power_brightness = 20;  // max. brightness when USB-PD fails to negotiate

void set_brightness(int value) {
	if (value < 0)
		value = 0;

	if (value > DISPLAY_WIDTH - 2)
		value = DISPLAY_WIDTH - 2;

	ESP_LOGD(T, "set_brightness(%d)", value);
	ledBrightness = value;
}

void init_rgb() {
	set_brightness(2);

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
	cfg.gpio_bus[10] = GPIO_E;
	cfg.gpio_bus[11] = GPIO_LAT;
	cfg.gpio_bus[12] = GPIO_OE_N;
	cfg.gpio_bus[13] = (gpio_num_t)(-1);
	cfg.gpio_bus[14] = (gpio_num_t)(-1);
	cfg.gpio_bus[15] = (gpio_num_t)(-1);

	cfg.bits = I2S_PARALLEL_BITS_16;
	cfg.bufa = bufdesc;
	cfg.bufb = NULL;

	//--------------------------
	// .json configuration
	//--------------------------
	// get `panel` dictionary
	cJSON *jPanel = jGet(getSettings(), "panel");

	// delay between updateFrame calls [ms]
	g_f_del = 1000 / jGetI(jPanel, "max_frame_rate", 30);

	// max brightness in low power mode
	low_power_brightness = jGetI(jPanel, "low_power_brightness", 20);

	// set clock divider
	cfg.clk_div = jGetI(jPanel, "clkm_div_num", 4);
	cfg.is_clk_inverted = jGetB(jPanel, "is_clk_inverted", true);

	//--------------------------
	// init the sub-frames
	//--------------------------
	for (int i = 0; i < BITPLANE_CNT; i++) {
		if (bitplane[i] == NULL) {
			bitplane[i] =
				(uint16_t *)heap_caps_malloc(BITPLANE_SZ * 2, MALLOC_CAP_DMA);
			assert(bitplane[i] && "Can't allocate bitplane memory");
		}
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

	updateFrame();

	// Setup I2S
	i2s_parallel_setup(&I2S1, &cfg);

	ESP_LOGI(T, "I2S setup done.");
}

void updateFrame() {
	// Check if we need to limit led brightness due to USB PD not giving 12 V
	int br = ledBrightness;
	bool is_bad = gpio_get_level(GPIO_PD_BAD);
	gpio_set_level(GPIO_LED, !is_bad);
	if (is_bad && br > low_power_brightness)
		br = low_power_brightness;

	// center the output enable between 2 strobes
	int oe_start = (DISPLAY_WIDTH - br) / 2;
	int oe_stop = (DISPLAY_WIDTH + br) / 2;

	lockFrameBuffer();

	for (unsigned int y = 0; y < DISPLAY_HEIGHT / 2; y++) {
		// Precalculate line bits of the *previous* line, which is the one we're
		// displaying now
		unsigned lbits = 0;

		if ((y - 1) & 1)
			lbits |= BIT_A;
		if ((y - 1) & 2)
			lbits |= BIT_B;
		if ((y - 1) & 4)
			lbits |= BIT_C;
		if ((y - 1) & 8)
			lbits |= BIT_D;
		if ((y - 1) & 16)
			lbits |= BIT_E;

		for (int x = 0; x < DISPLAY_WIDTH; x++) {
			int x_ = ESP32_TX_FIFO_POSITION_ADJUST(x);
			unsigned v = lbits;

			// Do not show image while the line bits are changing
			if (!(x_ >= oe_start && x_ < oe_stop))
				v |= BIT_OE_N;

			// latch pulse at the end of shifting in row - data
			if (x_ == (DISPLAY_WIDTH - 1))
				v |= BIT_LAT;

			// Does alpha blending of all graphical layers, a rather
			// expensive operation and best kept out of innermost loop.
			unsigned c1 = getBlendedPixel(x_, y);
			unsigned c2 = getBlendedPixel(x_, y + 16);

			for (int pl = 0; pl < BITPLANE_CNT; pl++) {
				// reset RGB bits
				unsigned v_ = v;

				// bitmask for pixel data in input for this bitplane
				unsigned mask = (1 << (8 - BITPLANE_CNT + pl));

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

	releaseFrameBuffer();
	g_frames++;
}




// ---------------
//  Test patterns
// ---------------
static void tp_stripes(unsigned width, unsigned offset, bool isY) {
	for (unsigned y = 0; y < DISPLAY_HEIGHT; y++) {
		for (unsigned x = 0; x < DISPLAY_WIDTH; x++) {
			unsigned var = isY ? x : y;
			unsigned col =
				(var + offset) % width == 0 ? 0xFFFFFFFF : 0xFF000000;
			setPixel(2, x, y, col);
		}
	}
	updateFrame();
}

static void tp_stripes_sequence(bool isY) {
	for (unsigned i = 0; i < 8; i++) {
		ESP_LOGD(T, "stripes %d / 8", i + 1);
		tp_stripes(8, i, isY);
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}
	for (unsigned i = 0; i < 4; i++) {
		ESP_LOGD(T, "stripes %d / 2", (i % 2) + 1);
		tp_stripes(2, i % 2, isY);
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}
}

void tp_task(void *pvParameters) {
	while (1) {
		setAll(0, 0xFF000000);
		setAll(1, 0xFF000000);
		setAll(2, 0xFF000000);
		updateFrame();

		ESP_LOGD(T, "Diagonal");
		for (unsigned y = 0; y < DISPLAY_HEIGHT; y++)
			for (unsigned x = 0; x < DISPLAY_WIDTH; x++)
				setPixel(
					2, x, y,
					(x - y) % DISPLAY_HEIGHT == 0 ? 0xFFFFFFFF : 0xFF000000
				);
		updateFrame();
		vTaskDelay(5000 / portTICK_PERIOD_MS);

		ESP_LOGD(T, "Vertical stripes ...");
		tp_stripes_sequence(true);

		ESP_LOGD(T, "Horizontal stripes ...");
		tp_stripes_sequence(false);

		ESP_LOGD(T, "All red");
		setAll(2, 0xFF0000FF);
		updateFrame();
		vTaskDelay(1000 / portTICK_PERIOD_MS);

		ESP_LOGD(T, "All green");
		setAll(2, 0xFF00FF00);
		updateFrame();
		vTaskDelay(1000 / portTICK_PERIOD_MS);

		ESP_LOGD(T, "All blue");
		setAll(2, 0xFFFF0000);
		updateFrame();
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}
