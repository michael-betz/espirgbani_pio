// The ESP32 Pinball Animation Clock

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "rom/rtc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "json_settings.h"
#include "wifi.h"

#include "common.h"
#include "rgb_led_panel.h"
#include "frame_buffer.h"
#include "animations.h"
#include "shaders.h"
#include "font.h"

static const char *T = "MAIN";

TaskHandle_t t_backg = NULL;
TaskHandle_t t_pinb = NULL;


void mount_sd_card(const char *path)
{
	// TODO if this fails, it really doesn't make sense to continue.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 20000;  // [kHz]

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = GPIO_SD_MOSI,
        .miso_io_num = GPIO_SD_MISO,
        .sclk_io_num = GPIO_SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA));

    sdmmc_card_t *card = NULL;

    sdspi_device_config_t device_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    device_cfg.gpio_cs = GPIO_SD_CS;
    device_cfg.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 32 * 1024
    };
    ESP_ERROR_CHECK(esp_vfs_fat_sdspi_mount("/sd", &host, &device_cfg, &mount_config, &card));

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
}

void app_main(void)
{
	//------------------------------
	// init stuff
	//------------------------------
	// forward serial characters to web-console
	// web_console_init();

	// report initial status
	ESP_LOGW(T,
		"reset reason: %d, heap: %ld, min_heap: %ld",
		rtc_get_reset_reason(0),
		esp_get_free_heap_size(),
		esp_get_minimum_free_heap_size()
	);

	// Mount spiffs for *.html and default_settings.json
	esp_vfs_spiffs_conf_t conf = {
		.base_path = "/spiffs",
		.partition_label = NULL,
		.max_files = 4,
		.format_if_mount_failed = true
	};
	esp_vfs_spiffs_register(&conf);

	// Mount SD for animations, fonts and for settings.json
	mount_sd_card("/sd");

	// Load settings.json from SD card, try to create file if it doesn't exist
	set_settings_file("/sd/settings.json", "/spiffs/default_settings.json");

	// init I2S driven rgb - panel
	init_rgb();
	updateFrame();

	// init web-server
	initWifi();
	tryJsonConnect();

	//------------------------------
	// Display test-patterns
	//------------------------------
	// only if enabled in json
	cJSON *jPanel = jGet(getSettings(), "panel");
	if (jGetB(jPanel, "test_pattern", true)) {
		ESP_LOGW(T, "RGB test-pattern mode!!!");
		g_rgbLedBrightness = MAX(0, jGetI(jPanel, "tp_brightness", 10));
		tp_task(NULL);
	}

	//------------------------------
	// Open animation file on SD
	//------------------------------
	FILE *f = fopen(ANIMATION_FILE, "r");
	if (!f) {
		ESP_LOGE(T, "fopen(%s, rb) failed: %s", ANIMATION_FILE, strerror(errno));
		ESP_LOGE(T, "Will not show animations!");
		vTaskDelete(NULL);  // kill current task (== return;)
	}

	//-----------------------------------
	// Startup animated background layer
	//-----------------------------------
	// this one calls updateFrame and hence
	// sets the global maximum frame-rate
	xTaskCreatePinnedToCore(&aniBackgroundTask, "bck", 1750, NULL, 1, &t_backg, 1);

	//---------------------------------
	// Draw animations and clock layer
	//---------------------------------
	xTaskCreatePinnedToCore(&aniPinballTask, "pin", 4000, f, 0, &t_pinb, 0);

	vTaskDelete(NULL);
}


void ws_callback(uint8_t *payload, unsigned len)
{
	char *tok = NULL;
	unsigned args[5];

	ESP_LOGI(T, "ws_callback(%d)", len);
	if (len < 1)
		return;

	// switch (payload[0]) {
	// case 'd':
	// 		char *p_tmp = (char*)(&payload[2]);
	// 		for(unsigned i = 0; i < 5; i++) {
	// 			tok = strsep(&p_tmp, ",");
	// 			if (tok == NULL) {
	// 				ESP_LOGE(T, "parse error!");
	// 				ESP_LOG_BUFFER_HEXDUMP(T, payload, len, ESP_LOG_ERROR);
	// 				return;
	// 			}
	// 			args[i] = strtoul(tok, NULL, 16);
	// 		}
	// 		setup_dds(args[0], args[1], args[2], args[3], args[4]);
	// 	break;
	// }

	// switch (data[0]) {
	// 	case 'a':
	// 		wsDumpRtc(client);  // read rolling log buffer in RTC memory
	// 		break;

	// 	case 'b':
	// 		settings_ws_handler(client, data, len);  // read / write settings.json
	// 		break;

	// 	case 'r':
	// 		ESP.restart();
	// 		break;

	// 	case 'h':
	// 		client->printf(
	// 			"h{\"heap\": %d, \"min_heap\": %d}",
	// 			esp_get_free_heap_size(), esp_get_minimum_free_heap_size()
	// 		);
	// 		break;
	// }
}
