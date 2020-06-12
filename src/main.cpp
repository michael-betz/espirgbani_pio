// demonstrates the use of esp-comms
#include <stdio.h>
#include "Arduino.h"
#include "SPIFFS.h"
#include "SPI.h"
#include "SD.h"
#include "rom/rtc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_comms.h"
#include "web_console.h"
#include "json_settings.h"

#include "common.h"
#include "rgb_led_panel.h"
#include "frame_buffer.h"
#include "animations.h"
#include "shaders.h"
#include "font.h"

TaskHandle_t t_backg = NULL;
TaskHandle_t t_clock = NULL;
TaskHandle_t t_pinb = NULL;

// Web socket RX data received callback
static void on_ws_data(
	AsyncWebSocket * server,
	AsyncWebSocketClient * client,
	AwsEventType type,
	void * arg,
	uint8_t *data,
	size_t len
) {
	switch (data[0]) {
		case 'a':
			wsDumpRtc(client);  // read rolling log buffer in RTC memory
			break;

		case 'b':
			settings_ws_handler(client, data, len);  // read / write settings.json
			break;

		case 'r':
			ESP.restart();
			break;

		case 'h':
			client->printf(
				"h{\"heap\": %d, \"min_heap\": %d}",
				esp_get_free_heap_size(), esp_get_minimum_free_heap_size()
			);
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

	// report initial status
	log_w(
		"reset reason: %d, heap: %d, min_heap: %d",
		rtc_get_reset_reason(0),
		esp_get_free_heap_size(),
		esp_get_minimum_free_heap_size()
	);

	// Mount spiffs for *.html and defaults.json
	SPIFFS.begin(true, "/spiffs", 10);
	log_w(
		"after SPIFFS, heap: %d, min_heap: %d",
		esp_get_free_heap_size(),
		esp_get_minimum_free_heap_size()
	);

	// Mount SD for animations, fonts and for settings.json
	SPI.begin(GPIO_SD_CLK, GPIO_SD_MISO, GPIO_SD_MOSI);
	bool ret = SD.begin(GPIO_SD_CS, SPI, 20 * 1000 * 1000, "/sd", 5);
	// Load settings.json from SD card, try to create file if it doesn't exist
	if (ret)
		set_settings_file("/sd/settings.json", "/spiffs/default_settings.json");

	log_w(
		"after SD, heap: %d, min_heap: %d",
		esp_get_free_heap_size(),
		esp_get_minimum_free_heap_size()
	);


	// init I2S driven rgb - panel
	init_rgb();
	updateFrame();
	log_w(
		"after RGB, heap: %d, min_heap: %d",
		esp_get_free_heap_size(),
		esp_get_minimum_free_heap_size()
	);

	// init web-server
	init_comms(SPIFFS, "/", on_ws_data);
	log_w(
		"after COMMS, heap: %d, min_heap: %d",
		esp_get_free_heap_size(),
		esp_get_minimum_free_heap_size()
	);

	//------------------------------
	// Display test-patterns
	//------------------------------
	// only if enabled in json
	cJSON *jPanel = jGet(getSettings(), "panel");
	if (jGetB(jPanel, "test_pattern", true)) {
		log_w("RGB test-pattern mode!!!");
		g_rgbLedBrightness = MAX(0, jGetI(jPanel, "tp_brightness", 10));
		tp_task(NULL);
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
	xTaskCreate(&aniBackgroundTask, "aniBackground", 1000, NULL, 1, &t_backg);

	//------------------------------
	// Startup clock layer
	//------------------------------
	delay(1000);
	xTaskCreate(&aniClockTask, "aniClock", 3000, NULL, 0, &t_clock);

	//------------------------------
	// Draw animations
	//------------------------------
	delay(1000);
	xTaskCreate(&aniPinballTask, "aniPinball", 2000, f, 0, &t_pinb);
}

void loop() {
	vTaskDelete(NULL);
}
