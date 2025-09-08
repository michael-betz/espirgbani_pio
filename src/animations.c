#include "animations.h"
#include "common.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fast_hsv2rgb.h"
#include "font.h"
#include "frame_buffer.h"
#include "json_settings.h"
#include "rom/rtc.h"
#include "wifi.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_wifi.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const char *T = "ANIMATIONS";

// Reads the filehader and fills `fh`
static int getFileHeader(FILE *f, fileHeader_t *fh) {
	char tempCh[3];

	if (f == NULL || fh == NULL)
		return -1;

	fseek(f, 0x00000000, SEEK_SET);
	fread(tempCh, 3, 1, f);
	if (memcmp(tempCh, "DGD", 3) != 0) {
		ESP_LOGE(T, "Invalid file header!");
		return -1;
	}
	fread(&fh->nAnimations, 2, 1, f);
	fh->nAnimations = SWAP16(fh->nAnimations);
	fseek(f, 0x000001EF, SEEK_SET);
	fread(&fh->buildStr, 8, 1, f);
	fh->buildStr[8] = '\0';

	ESP_LOGI(T, "nAnimations: %d, buildStr: %s", fh->nAnimations, fh->buildStr);
	return 0;
}

// Fills the headerEntry struct with data.
// Specify an `headerIndex` from 0 to nAnimations
static int _cur_hi = 0;
static int readHeaderEntry(FILE *f, headerEntry_t *h, int headerIndex) {
	if (f == NULL || h == NULL)
		return -1;

	_cur_hi = headerIndex;

	// Copys header info into h. Note that h->nFrameEntries must be freed!
	fseek(f, HEADER_OFFS + HEADER_SIZE * headerIndex, SEEK_SET);
	fread(h, sizeof(*h), 1, f);
	h->name[31] = '\0';
	h->frameHeader = NULL;
	h->animationId = SWAP16(h->animationId);
	h->byteOffset = SWAP32(h->byteOffset) * HEADER_SIZE;
	// Exract frame header
	frameHeaderEntry_t *fh = (frameHeaderEntry_t *)malloc(
		sizeof(frameHeaderEntry_t) * h->nFrameEntries
	);
	if (fh == NULL) {
		ESP_LOGE(T, "Memory allocation error!");
		return -1;
	}
	h->frameHeader = fh;
	fseek(f, h->byteOffset, SEEK_SET);
	for (int i = 0; i < h->nFrameEntries; i++) {
		fread(&fh->frameId, 1, 1, f);
		fread(&fh->frameDur, 1, 1, f);
		// Hack to sort out invalid headers
		if (fh->frameDur <= 0 || fh->frameId > h->nStoredFrames) {
			ESP_LOGE(T, "Invalid header!");
			return -1;
		}
		fh++;
	}
	return 0;
}

// seek the file f to the beginning of a specific animation frame
static void seekToFrame(FILE *f, int byteOffset, int frameId) {
	// without fast-seek enabled, this sometimes takes hundreds of ms,
	// resulting in choppy animation playback
	// http://www.elm-chan.org/fsw/ff/doc/lseek.html
	if (f == NULL || frameId <= 0)
		return;
	byteOffset += DISPLAY_WIDTH * DISPLAY_HEIGHT * (frameId - 1) / 2;
	fseek(f, byteOffset, SEEK_SET);
}

// play a single animation, start to finish
static void playAni(FILE *f, headerEntry_t *h) {
	int64_t seek_time = 0;
	int max_seek_time = 0;

	int64_t draw_time = 0;
	int max_draw_time = 0;
	int sum_draw_time = 0;

	if (f == NULL || h == NULL || h->nFrameEntries == 0)
		return;

	// get a random color
	uint8_t r, g, b;
	fast_hsv2rgb_32bit(
		RAND_AB(0, HSV_HUE_MAX), HSV_SAT_MAX, HSV_VAL_MAX, &r, &g, &b
	);
	unsigned color = SRGBA(r, g, b, 0xFF);

	// pre-seek the file to beginning of frame
	frameHeaderEntry_t fh = h->frameHeader[0];
	seekToFrame(f, h->byteOffset + HEADER_SIZE, fh.frameId);
	unsigned cur_delay = fh.frameDur;
	TickType_t xLastWakeTime = xTaskGetTickCount();

	for (int i = 0; i < h->nFrameEntries; i++) {
		draw_time = esp_timer_get_time();
		if (fh.frameId <= 0)
			setAll(2, 0xFF000000); // invalid frame = translucent black
		else
			setFromFile(f, 2, color);
		draw_time = esp_timer_get_time() - draw_time;
		sum_draw_time += draw_time;
		if (draw_time > max_draw_time)
			max_draw_time = draw_time;

		// get the next frame ready in advance
		if (i < h->nFrameEntries - 1) {
			fh = h->frameHeader[i + 1];

			seek_time = esp_timer_get_time();
			seekToFrame(f, h->byteOffset + HEADER_SIZE, fh.frameId);
			seek_time = esp_timer_get_time() - seek_time;
			if (seek_time > max_seek_time)
				max_seek_time = seek_time;
		}

		// clip minimum delay to avoid skipping frames
		if (cur_delay < g_f_del)
			cur_delay = g_f_del;

		// wait for N ms, measured from last call to vTaskDelayUntil()
		vTaskDelayUntil(&xLastWakeTime, cur_delay / portTICK_PERIOD_MS);
		cur_delay = fh.frameDur;
	}
	ESP_LOGD(
		T, "%d, %s, f: %d / %d, d: %d ms, seek: %d ms, draw: %d / %d ms",
		_cur_hi, h->name, h->nStoredFrames, h->nFrameEntries,
		h->frameHeader->frameDur, max_seek_time / 1000,
		sum_draw_time / h->nFrameEntries / 1000, max_draw_time / 1000
	);
}

// blocks while rendering a pinball animation, with fade-out.
// takes care of loading header data
static void run_animation(FILE *f, unsigned aniId) {
	TickType_t xLastWakeTime;
	headerEntry_t myHeader;

	if (f == NULL)
		return;

	readHeaderEntry(f, &myHeader, aniId);
	playAni(f, &myHeader);
	free(myHeader.frameHeader);
	myHeader.frameHeader = NULL;

	// Keep a single frame displayed for a bit
	if (myHeader.nStoredFrames <= 3 || myHeader.nFrameEntries <= 3)
		vTaskDelay(3000 / portTICK_PERIOD_MS);

	// Fade out the frame
	uint32_t nTouched = 1;
	xLastWakeTime = xTaskGetTickCount();
	while (nTouched) {
		nTouched = fadeOut(2, 10);
		vTaskDelayUntil(&xLastWakeTime, g_f_del / portTICK_PERIOD_MS);
	}
}

adc_oneshot_unit_handle_t adc_handle;

void init_light_sensor() {
	adc_oneshot_unit_init_cfg_t init_config = {
		.unit_id = ADC_UNIT_1,
	};
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

	//-------------ADC1 Config---------------//
	adc_oneshot_chan_cfg_t config = {
		.atten = ADC_ATTEN_DB_12,	 // VMAX = 2450
		.bitwidth = ADC_BITWIDTH_12, // 12 bit, DMAX = 4095
	};
	// ADC_UNIT_1, ADC_CHANNEL_0 is connected to SENSOR_VP pin
	ESP_ERROR_CHECK(
		adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &config)
	);
}

int get_light_sensor() {
	// 8x mode, actually 0 .. 32k now
	//   10 lux,   3 uA, 0.03 V,   50, Twilight, candle light
	//   50 lux,         0.17 V,  284, Street light at night
	//  100 lux,  33 uA, 0.33 V,  550, Dark museum
	// 1000 lux, 330 uA, 3.30 V, 4095, Bright office lightning
	int sum = 0;
	for (unsigned i = 0; i < 8; i++) {
		int tmp = 0;
		adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &tmp);
		sum += tmp;
	}
	return sum;
}

// Called once a minute
static void manageBrightness(struct tm *timeinfo) {
	static int is_daylight_state = -1;

	// get json dictionaries
	cJSON *jPow = jGet(getSettings(), "power");
	int mode = jGetI(jPow, "mode", 0);
	cJSON *jDay = jGet(jPow, "day");

	if (mode == 1) {
		// use ambient light sensor for brightness control
		// < 300: minimum brightness (1)
		// 300 - 4095: brightness from (1 - 100)
		int ambient_light = get_light_sensor(); // 0 .. 32k

		int offset = jGetI(jPow, "offset", 0);
		int divider = jGetI(jPow, "divider", 327);
		int max_limit = jGetI(jPow, "max_limit", 100);
		int min_limit = jGetI(jPow, "min_limit", 5);
		int raw_value = ambient_light - offset;
		raw_value /= divider;
		if (raw_value < min_limit)
			raw_value = min_limit;
		if (raw_value <= 0)
			raw_value = 1;
		if (raw_value > max_limit)
			raw_value = max_limit;
		ESP_LOGI(
			T, "Ambient light: %d,  brightness: %d", ambient_light, raw_value
		);
		set_brightness(raw_value); // 1 - 100
	} else if (mode == 2) {
		// use day and night switching times
		// convert times to minutes since 00:00
		cJSON *jNight = jGet(jPow, "night");
		int iDay = jGetI(jDay, "h", 9) * 60 + jGetI(jDay, "m", 0);
		int iNight = jGetI(jNight, "h", 22) * 60 + jGetI(jNight, "m", 45);
		int iNow = timeinfo->tm_hour * 60 + timeinfo->tm_min;

		if (iNow >= iDay && iNow < iNight) {
			if (is_daylight_state != 1) {
				int tmp = jGetI(jDay, "p", 20);
				set_brightness(tmp);
				ESP_LOGI(T, "Daylight mode, p: %d", tmp);
				is_daylight_state = 1;
			}
		} else {
			if (is_daylight_state != 0) {
				int tmp = jGetI(jNight, "p", 2);
				set_brightness(tmp);
				ESP_LOGI(T, "Nightdark mode, p: %d", tmp);
				is_daylight_state = 0;
			}
		}
	} else { // mode 0 or everything else
		set_brightness(jGetI(jDay, "p", 20));
	}
}

static void stats(unsigned cur_fnt) {
	RTC_NOINIT_ATTR static unsigned max_uptime; // [minutes]
	static unsigned last_frames = 0;
	static unsigned last_time = 0;

	unsigned cur_time = esp_timer_get_time() / 1000;
	unsigned up_time = cur_time / 1000 / 60;
	float fps = 1000.0 * (g_frames - last_frames) / (cur_time - last_time);
	last_time = cur_time;
	last_frames = g_frames;

	// reset max_uptime counter on power cycle
	if (up_time == 0)
		if (rtc_get_reset_reason(0) == POWERON_RESET ||
			rtc_get_reset_reason(0) == RTCWDT_RTC_RESET)
			max_uptime = 0;

	// print uptime stats
	if (up_time > max_uptime)
		max_uptime = up_time;

	ESP_LOGD(
		T,
		"fnt: %d, uptime: %d / %d, fps: %.1f, heap: %ld / %ld, ba: %d, pi: %d",
		cur_fnt, up_time, max_uptime, fps, esp_get_free_heap_size(),
		esp_get_minimum_free_heap_size(), uxTaskGetStackHighWaterMark(t_backg),
		uxTaskGetStackHighWaterMark(t_pinb)
	);
}

// Returns the number of consecutive `path/0.fnt` files
static int cntFntFiles(const char *path) {
	int nFiles = 0;
	char fNameBuffer[32];
	struct stat buffer;
	while (1) {
		sprintf(fNameBuffer, "%s/%03d.fnt", path, nFiles);
		if (stat(fNameBuffer, &buffer) == 0) {
			nFiles++;
		} else {
			break;
		}
	}
	return nFiles;
}

static void show_wifi_state() {
	push_print(WHITE, "\nWIFI: ");
	switch (wifi_state) {
	case WIFI_NOT_CONNECTED:
		push_print(RED, "disconnected");
		break;
	// case WIFI_SCANNING:
	// 	push_print(BLUE, "scanning ...");
	// 	break;
	case WIFI_DPP_LISTENING:
		push_print(BLUE, "DPP mode ...");
		break;
	case WIFI_CONNECTED:
		push_print(GREEN, "%s", wifi_ssid);
		push_print(WHITE, "\nIP: " IPSTR, IP2STR(&wifi_ip));
		break;
	case WIFI_AP_MODE:
		push_print(WHITE, "AP ");
		push_print(GREEN, "%s", wifi_ssid);
		break;
	}
}

// takes care of drawing pinball animations (layer 2) and the clock (layer 1)
void aniPinballTask(void *pvParameters) {
	unsigned cycles = 0;
	static int sec_ = 0;
	static int wifi_state_last = -1;

	TickType_t xLastWakeTime = xTaskGetTickCount();

	// init built in font
	setAll(1, 0x00000000);
	initFont("/spiffs/lemon.fnt");

	const char *hostname = jGetS(getSettings(), "hostname", "espirgbani");
	push_str(
		DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 4, hostname, 32, A_CENTER, 1,
		WHITE, false
	);
	vTaskDelay(3000 / portTICK_PERIOD_MS);
	setAll(1, 0);

	init_print();
	push_print(WHITE, "\nUSB power level: ");
	if (gpio_get_level(GPIO_PD_BAD))
		push_print(RED, "Low");
	else
		push_print(GREEN, "High");

	//------------------------------
	// Open animation file on SD card
	//------------------------------
	push_print(WHITE, "\nLoading animations ...");
	fileHeader_t fh;
	FILE *fAnimations = fopen(ANIMATION_FILE, "r");
	if (fAnimations == NULL) {
		ESP_LOGE(
			T, "fopen(%s, rb) failed: %s", ANIMATION_FILE, strerror(errno)
		);
		ESP_LOGE(T, "Will not show animations!");
		push_print(RED, "\n%s", strerror(errno));
		vTaskDelay(5000 / portTICK_PERIOD_MS);
	} else {
		if (setvbuf(fAnimations, NULL, _IOFBF, 512) != 0)
			ESP_LOGW(
				T, " setvbuf(%s, 512) failed: %s", ANIMATION_FILE,
				strerror(errno)
			);

		getFileHeader(fAnimations, &fh);
		push_print(GREEN, "\n  N: %d  B: %s", fh.nAnimations, fh.buildStr);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}

	show_wifi_state();
	wifi_state_last = wifi_state;

	//------------------------------
	// Count font files on SD card
	//------------------------------
	struct tm timeinfo;
	char strftime_buf[64];
	unsigned color = 0;

	// count font files and choose a random one
	push_print(WHITE, "\nLoading fonts ...");
	int nFnts = cntFntFiles("/sd/fnt");
	if (nFnts <= 0) {
		ESP_LOGE(T, "no fonts found on SD card :( :( :(");
		push_print(RED, "\n  No fonts found :(");
		vTaskDelay(5000 / portTICK_PERIOD_MS);
	} else {
		push_print(GREEN, " N: %d", nFnts);
		ESP_LOGI(T, "last font file: /sd/fnt/%03d.fnt", nFnts - 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
	push_print(WHITE, "\nLet's go !!!");
	restore_print();
	vTaskDelay(2000 / portTICK_PERIOD_MS);

	//------------------------------
	// Load configuration
	//------------------------------
	cJSON *jDelay = jGet(getSettings(), "delays");

	// delay between animations
	unsigned ani_delay = jGetI(jDelay, "ani", 15);
	ani_delay = MAX(1, ani_delay);

	// delay between font changes
	unsigned font_delay = jGetI(jDelay, "font", 3600);
	font_delay = MAX(1, font_delay);

	// delay between font color changes
	unsigned color_delay = jGetI(jDelay, "color", 600);
	color_delay = MAX(1, color_delay);

	init_light_sensor();

	unsigned cur_fnt = 0;

	while (1) {
		bool doRedrawFont = false;

		// draw an animation
		if (fAnimations && cycles > 0 && cycles % ani_delay == 0) {
			unsigned aniId = RAND_AB(0, fh.nAnimations - 1);
			// aniId = 0x0619;
			run_animation(fAnimations, aniId);
		}

		// change font color every delays.color minutes
		if (cycles % color_delay == 0) {
			color = 0xFF000000 | rand();
			doRedrawFont = true;
		}

		// change font every delays.font minutes
		if (nFnts > 0 && cycles % font_delay == 0) {
			cur_fnt = RAND_AB(0, nFnts - 1);
			sprintf(strftime_buf, "/sd/fnt/%03d.fnt", cur_fnt);
			// cur_fnt = (cur_fnt + 1) % nFnts;
			initFont(strftime_buf);
			doRedrawFont = true;
		}

		// get the wall-clock time
		time_t now = 0;
		time(&now);
		localtime_r(&now, &timeinfo);

		// Redraw the clock when tm_sec rolls over
		if (doRedrawFont || sec_ > timeinfo.tm_sec) {
			strftime(strftime_buf, sizeof(strftime_buf), "%H:%M", &timeinfo);
			// randomly colored outline, black filling
			drawStrCentered(strftime_buf, color, 0xFF000000);

			manageBrightness(&timeinfo);
			stats(cur_fnt);

			// Every minute if wifi is not connected (also not in AP mode)
			if (wifi_state == WIFI_NOT_CONNECTED)
				tryJsonConnect();
		}
		sec_ = timeinfo.tm_sec;

		// Button toggles between AP mode and Client mode
		static bool lvl_ = false;
		bool lvl = gpio_get_level(GPIO_WIFI); // active low
		if (cycles > 0 && lvl_ && !lvl) {
			if (wifi_state == WIFI_AP_MODE)
				tryJsonConnect();
			else
				tryApMode();
			// tryEasyConnect();
		}
		lvl_ = lvl;

		if (wifi_state != wifi_state_last) {
			init_print();
			show_wifi_state();
			wifi_state_last = wifi_state;
			restore_print();
			vTaskDelay(2000 / portTICK_PERIOD_MS);
		}

		cycles++;
		vTaskDelayUntil(&xLastWakeTime, 1000 / portTICK_PERIOD_MS);
	}

	if (fAnimations)
		fclose(fAnimations);
	fAnimations = NULL;

	vTaskDelete(NULL);
}
