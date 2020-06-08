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

// !!! HARDWARE BUG !!!
// according to
// https://randomnerdtutorials.com/esp32-pinout-reference-gpios/
// GPIO12 must be low during boot!
// unfortunately the SD card leaves MISO = GPIO12 IDLE high.
// ... the ESP will get stuck in a boot loop after a hard reset when the
// SD card is present.
#define GPIO_SD_MISO 12
#define GPIO_SD_CS   14
#define GPIO_SD_MOSI 32
#define GPIO_SD_CLK  33

// Web socket RX callback
static void onMsg(websockets::WebsocketsMessage msg)
{
	if (msg.c_str()[0] == 'a') wsDumpRtc();
	settings_ws_handler(msg);
}

void setup()
{
	// forwards each serial character to web-console
	ets_install_putc2(wsDebugPutc);

	// Mount spiffs for .html and defaults .json
	SPIFFS.begin(true, "/spiffs", 5);

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

	init_comms(false, SPIFFS, "/", onMsg);
}

void loop() {
	static unsigned cycle=0;

	refresh_comms();  // only needed when init_comms(false, ...)

	cycle++;
	delay(100);
}
