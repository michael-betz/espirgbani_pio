// demonstrates the use of esp-comms
#include <stdio.h>
#include "Arduino.h"
#include "ArduinoWebsockets.h"
#include "SPIFFS.h"
#include "SPI.h"
#include "SD.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "comms.h"
#include "web_console.h"
#include "json_settings.h"

#include "common.h"
#include "rgb_led_panel.h"
#include "frame_buffer.h"
#include "animations.h"

// Web socket RX callback
static void onMsg(websockets::WebsocketsMessage msg)
{
	if (msg.c_str()[0] == 'a') wsDumpRtc();
	settings_ws_handler(msg);
}

void setup()
{
	//------------------------------
	// init stuff
	//------------------------------
	// forwards each serial character to web-console
	ets_install_putc2(wsDebugPutc);

	// Mount spiffs for .html and defaults .json
	SPIFFS.begin(true, "/spiffs", 10);

	// Mount SD for animations, fonts and for settings.json
	SPI.begin(GPIO_SD_CLK, GPIO_SD_MISO, GPIO_SD_MOSI);
	bool ret = SD.begin(GPIO_SD_CS, SPI, 20 * 1000 * 1000, "/sd", 5);
	if (!ret) {
		log_e("SD-card mount Failed :( :( :(");
		set_settings_file("/spiffs/default_settings.json", NULL);
	} else {
		// When settings.json cannot be opened, try to copy the default_settings over
		set_settings_file("/sd/settings.json", "/spiffs/default_settings.json");
	}

	// init I2S driven rgb - panel
	init_rgb();
	updateFrame();

	// init web-server
	init_comms(true, SPIFFS, "/", onMsg);

	//------------------------------
	// display test-pattern
	//------------------------------
	cJSON *jPanel = jGet(getSettings(), "panel");
	g_rgbLedBrightness = jGetI(jPanel, "tp_brightness", 10);
	if (g_rgbLedBrightness < 1) g_rgbLedBrightness = 1;
	// only if enabled in json
	if (jGetB(jPanel, "test_pattern", true)) {
		while(1) {
			log_i("RGB test-pattern mode!!!");
			tp_sequence();
		}
	}

	//------------------------------
	// Read animation file from SD
	//------------------------------
	FILE *f = fopen(ANIMATION_FILE, "r");
	if (!f) {
		log_e("fopen(%s, rb) failed: %s", ANIMATION_FILE, strerror(errno));
		log_e("Will not show animations!");
		vTaskDelete(NULL);  // kill current task (== return;)
	}

	fileHeader_t fh;
	getFileHeader(f, &fh);
	fseek(f, HEADER_OFFS, SEEK_SET);
	headerEntry_t myHeader;

	int aniId;
	cJSON *jDelay = jGet(getSettings(), "delays");

	while (1) {
		aniId = RAND_AB(0, fh.nAnimations-1);
		readHeaderEntry(f, &myHeader, aniId);
		playAni(f, &myHeader);
		free(myHeader.frameHeader);
		myHeader.frameHeader = NULL;

		// Keep a single frame displayed for a bit
		if (myHeader.nStoredFrames <= 3 || myHeader.nFrameEntries <= 3)
			delay(3000);

		// Fade out the frame
		uint32_t nTouched = 1;
		while (nTouched) {
			// startDrawing(2);
			nTouched = fadeOut(2, 10);
			// doneDrawing(2);
			updateFrame();
			delay(20);
		}

		// startDrawing(2);
		setAll(2, 0x00000000); // Make layer fully transparent
		// doneDrawing(2);
		updateFrame();

		delay(jGetI(jDelay, "ani", 15) * 1000);
	}
	fclose(f);
}

void loop() {
	static unsigned cycle=0;
	// refresh_comms();  // only needed when init_comms(false, ...)
	cycle++;
	delay(10);
}
