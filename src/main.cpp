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
#include "shaders.h"
#include "font.h"

// Web-socket RX callback
static void onMsg(websockets::WebsocketsMessage msg)
{
	if (msg.length() <= 0)
		return;

	switch (msg.c_str()[0]) {
		case 'a':
			wsDumpRtc();  // read rolling log buffer in RTC memory
			break;
		case 'b':
			settings_ws_handler(msg);  // read / write settings.json
			break;
		case 'r':
			ESP.restart();
			break;
	}
}

void setup()
{
	//------------------------------
	// init stuff
	//------------------------------
	// forward serial characters to web-console
	web_console_init();

	// Mount spiffs for *.html and defaults.json
	SPIFFS.begin(true, "/spiffs", 10);

	// Mount SD for animations, fonts and for settings.json
	SPI.begin(GPIO_SD_CLK, GPIO_SD_MISO, GPIO_SD_MOSI);
	bool ret = SD.begin(GPIO_SD_CS, SPI, 20 * 1000 * 1000, "/sd", 5);
	if (!ret) {
		log_e("SD-card mount Failed :( :( :(");
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
	// Display test-patterns
	//------------------------------
	cJSON *jPanel = jGet(getSettings(), "panel");
	g_rgbLedBrightness = jGetI(jPanel, "brightness", 10);
	if (g_rgbLedBrightness < 1) g_rgbLedBrightness = 1;
	// only if enabled in json
	if (jGetB(jPanel, "test_pattern", true)) {
		while(1) {
			log_i("RGB test-pattern mode!!!");
			tp_sequence();
		}
	}

	//------------------------------
	// Open animation file on SD
	//------------------------------
	FILE *f = fopen(ANIMATION_FILE, "r");
	if (!f) {
		log_e("fopen(%s, rb) failed: %s", ANIMATION_FILE, strerror(errno));
		log_e("Will not show animations!");
		vTaskDelete(NULL);  // kill current task (== return;)
	}

	//-----------------------------------
	// Startup animated background layer
	//-----------------------------------
	// this one calls updateFrame and hence
	// sets the global maximum frame-rate
	delay(1000);
	xTaskCreate(&aniBackgroundTask, "aniBackground", 4096, NULL, 0, NULL);

	//------------------------------
	// Startup clock layer
	//------------------------------
	delay(1000);
	xTaskCreate(&aniClockTask, "aniClock", 4096, NULL, 0, NULL);

	//------------------------------
	// Draw animations
	//------------------------------
	// blocks forever ...
	delay(1000);
	aniPinballTask(f);
}

void loop() {vTaskDelete(NULL);}
