#include <string.h>
#include "Arduino.h"
#include "fast_hsv2rgb.h"
#include "frame_buffer.h"
#include "json_settings.h"
#include <ArduinoOTA.h>
#include "rom/rtc.h"
#include "font.h"
#include "animations.h"

// Reads the filehader and fills `fh`
static int getFileHeader(FILE *f, fileHeader_t *fh)
{
	char tempCh[3];

	if (f == NULL || fh == NULL)
		return -1;

	fseek(f, 0x00000000, SEEK_SET);
	fread(tempCh, 3, 1, f);
	if(memcmp(tempCh,"DGD", 3) != 0) {
		log_e("Invalid file header!");
		return -1;
	}
	fread(&fh->nAnimations, 2, 1, f);
	fh->nAnimations = SWAP16(fh->nAnimations);
	fseek(f, 0x000001EF, SEEK_SET);
	fread(&fh->buildStr, 8, 1, f);

	log_i("Build: %s, Found %d animations", fh->buildStr, fh->nAnimations);
	return 0;
}

// Fills the headerEntry struct with data.
// Specify an `headerIndex` from 0 to nAnimations
static int readHeaderEntry(FILE *f, headerEntry_t *h, int headerIndex)
{
	if (f == NULL || h == NULL)
		return -1;

	// Copys header info into h. Note that h->nFrameEntries must be freed!
	fseek(f, HEADER_OFFS + HEADER_SIZE * headerIndex, SEEK_SET);
	fread(h, sizeof(*h), 1, f);
	h->name[31] = '\0';
	h->frameHeader = NULL;
	h->animationId = SWAP16(h->animationId);
	h->byteOffset =  SWAP32(h->byteOffset) * HEADER_SIZE;
	// Exract frame header
	frameHeaderEntry_t *fh = (frameHeaderEntry_t*)malloc(sizeof(frameHeaderEntry_t) * h->nFrameEntries);
	if (fh == NULL) {
		log_e("Memory allocation error!");
		return -1;
	}
	h->frameHeader = fh;
	fseek(f, h->byteOffset, SEEK_SET);
	for(int i=0; i<h->nFrameEntries; i++) {
		fread(&fh->frameId,  1, 1, f);
		fread(&fh->frameDur, 1, 1, f);
		// Hack to sort out invalid headers
		if(fh->frameDur <= 0 || fh->frameId > h->nStoredFrames) {
			log_e("Invalid header!");
			return -1;
		}
		fh++;
	}

	log_d(
		"%s: index: %d, w: %d, h: %d, nMem: %d, nAni: %d, del: %d",
		h->name,
		headerIndex,
		h->width,
		h->height,
		h->nStoredFrames,
		h->nFrameEntries,
		h->frameHeader->frameDur
	);
	return 0;
}

// seek the file f to the beginning of a specific animation frame
static void seekToFrame(FILE *f, int byteOffset, int frameId)
{
	// without fast-seek enabled, this sometimes takes hundreds of ms,
	// resulting in choppy animation playback
	// http://www.elm-chan.org/fsw/ff/doc/lseek.html
	if (f == NULL || frameId <= 0)
		return;
	byteOffset += DISPLAY_WIDTH * DISPLAY_HEIGHT * (frameId - 1) / 2;
	fseek(f, byteOffset, SEEK_SET);
}

// play a single animation, start to finish
// lock_fb: synchronize with updateFrame (vsync), prevents artifacts, reduces frame rate
// f_del: delay in [ms] between updateFrame calls (determines global maximum framerate)
static void playAni(FILE *f, headerEntry_t *h, bool lock_fb, unsigned f_del)
{
	int64_t seek_time = 0;
	int max_seek_time = 0;

	int64_t draw_time = 0;
	int max_draw_time = 0;
	int sum_draw_time = 0;

	if (f == NULL || h == NULL || h->nFrameEntries == 0)
		return;

	// get a random color
	uint8_t r, g, b;
	fast_hsv2rgb_32bit(RAND_AB(0,HSV_HUE_MAX), HSV_SAT_MAX, HSV_VAL_MAX, &r, &g, &b);
	unsigned color = SRGBA(r,g,b,0xFF);

	// pre-seek the file to beginning of frame
	frameHeaderEntry_t fh = h->frameHeader[0];
	seekToFrame(f, h->byteOffset + HEADER_SIZE, fh.frameId);
	unsigned cur_delay = fh.frameDur;
	TickType_t xLastWakeTime = xTaskGetTickCount();

	for(int i=0; i<h->nFrameEntries; i++) {
		draw_time = esp_timer_get_time();
		if(fh.frameId <= 0)
			setAll(2, 0xFF000000);  // invalid frame = translucent black
		else
			setFromFile(f, 2, color, lock_fb);
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
		if (cur_delay < f_del)
			cur_delay = f_del;

		// wait for N ms, measured from last call to vTaskDelayUntil()
		vTaskDelayUntil(&xLastWakeTime, cur_delay / portTICK_PERIOD_MS);
		cur_delay = fh.frameDur;
	}
	log_d(
		"max_seek: %d us,  mean_draw: %d us,  max_draw: %d us",
		max_seek_time,
		sum_draw_time / h->nFrameEntries,
		max_draw_time
	);
}

// blocks while rendering a pinball animation, with fade-out.
// takes care of loading header data
static void run_animation(FILE *f, unsigned aniId)
{
	TickType_t xLastWakeTime;
	headerEntry_t myHeader;

	// synchronize animation frames to global update rate (vsync)
	cJSON *jPanel = jGet(getSettings(), "panel");
	bool lock_fb = jGetB(jPanel, "lock_frame_buffer", true);

	readHeaderEntry(f, &myHeader, aniId);
	playAni(f, &myHeader, lock_fb, g_f_del);
	free(myHeader.frameHeader);
	myHeader.frameHeader = NULL;

	// Keep a single frame displayed for a bit
	if (myHeader.nStoredFrames <= 3 || myHeader.nFrameEntries <= 3)
		delay(3000);

	// Fade out the frame
	uint32_t nTouched = 1;
	xLastWakeTime = xTaskGetTickCount();
	while (nTouched) {
		startDrawing(2);
		nTouched = fadeOut(2, 10);
		doneDrawing(2);
		vTaskDelayUntil(&xLastWakeTime, 30 / portTICK_PERIOD_MS);
	}
}

static void manageBrightness(struct tm *timeinfo)
{
	// get json dictionaries
	cJSON *jPow = jGet(getSettings(), "power");
	cJSON *jDay = jGet(jPow, "day");
	cJSON *jNight = jGet(jPow, "night");

	// convert times to minutes since 00:00
	int iDay = jGetI(jDay, "h", 9) * 60 + jGetI(jDay, "m", 0);
	int iNight = jGetI(jNight,"h", 22) * 60 + jGetI(jNight, "m", 45);
	int iNow = timeinfo->tm_hour * 60 + timeinfo->tm_min;

	if (iNow >= iDay && iNow < iNight) {
		// Daylight mode
		g_rgbLedBrightness = jGetI(jDay, "p", 20);
	} else {
		// Nightdark mode
		g_rgbLedBrightness = jGetI(jNight, "p", 2);
	}
}

static void stats()
{
	RTC_NOINIT_ATTR static unsigned max_uptime;  // [minutes]
	static unsigned last_frames = 0;
	static unsigned last_time = 0;

	unsigned cur_time = millis();
	unsigned up_time = cur_time / 1000 / 60;
	float fps = 1000.0 * (g_frames - last_frames) / (cur_time - last_time);
	last_time = cur_time;
	last_frames = g_frames;

	// reset max_uptime counter on power cycle
	if (up_time == 0)
		if (rtc_get_reset_reason(0) == POWERON_RESET || rtc_get_reset_reason(0) == RTCWDT_RTC_RESET)
			max_uptime = 0;

	// print uptime stats
	if (up_time > max_uptime)
		max_uptime = up_time;

	log_i("uptime: %d / %d, fps: %.1f, heap: %d / %d, stack ba: %d, pi: %d",
		up_time,
		max_uptime,
		fps,
		esp_get_free_heap_size(),
		esp_get_minimum_free_heap_size(),
		uxTaskGetStackHighWaterMark(t_backg),
		uxTaskGetStackHighWaterMark(t_pinb)
	);
}

// takes care of drawing pinball animations (layer 2) and the clock (layer 1)
void aniPinballTask(void *pvParameters)
{
	unsigned cycles = 0;
	TickType_t xLastWakeTime = xTaskGetTickCount();

	// Pinball animation stuff
	FILE *f = (FILE *)pvParameters;
	fileHeader_t fh;
	getFileHeader(f, &fh);

	// Font and time stuff
	struct tm timeinfo;
	char strftime_buf[64];
	unsigned color = 0;

	// count font files and choose a random one
	int maxFnt = cntFntFiles("/sd/fnt") - 1;
	if (maxFnt < 0)
		log_e("no fonts found on SD card :( :( :(");
	else
		log_i("last font file: /sd/fnt/%d.fnt", maxFnt);


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


	while (1) {
		bool doRedrawFont = false;

		// draw an animation
		if ((cycles % ani_delay) == 0) {
			unsigned aniId = RAND_AB(0, fh.nAnimations - 1);
			// aniId = 0x0619;
			run_animation(f, aniId);
		}

		// change font color every delays.color minutes
		if ((cycles % color_delay) == 0) {
			color = 0xFF000000 | rand();
			doRedrawFont = true;
		}

		// change font every delays.font minutes
		if (maxFnt > 0 && (cycles % font_delay) == 0) {
			sprintf(strftime_buf, "/sd/fnt/%d", RAND_AB(0, maxFnt));
			initFont(strftime_buf);
			doRedrawFont = true;
		}

		// get the wall-clock time
		time_t now = 0;
		time(&now);
		localtime_r(&now, &timeinfo);

		// Redraw the clock
		if (doRedrawFont || timeinfo.tm_sec == 0) {
			strftime(strftime_buf, sizeof(strftime_buf), "%H:%M", &timeinfo);
			drawStrCentered(strftime_buf, 1, color, 0xFF000000);
			manageBrightness(&timeinfo);
			stats();
		}

		ArduinoOTA.handle();

		cycles++;

		vTaskDelayUntil(&xLastWakeTime, 1000 / portTICK_PERIOD_MS);
	}
	fclose(f);
}
