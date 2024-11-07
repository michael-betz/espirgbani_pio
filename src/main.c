// The ESP32 Pinball Animation Clock

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/rtc.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "esp_wifi.h"
#include "json_settings.h"
#include "wifi.h"
#include "static_ws.h"

#include "animations.h"
#include "common.h"
#include "font.h"
#include "frame_buffer.h"
#include "rgb_led_panel.h"
#include "shaders.h"

static const char *T = "MAIN";

TaskHandle_t t_backg = NULL;
TaskHandle_t t_pinb = NULL;

void mount_sd_card(const char *path) {
	// TODO if this fails, it really doesn't make sense to continue.
	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	host.max_freq_khz = 20000; // [kHz]

	spi_bus_config_t bus_cfg = {
		.mosi_io_num = GPIO_SD_MOSI,
		.miso_io_num = GPIO_SD_MISO,
		.sclk_io_num = GPIO_SD_CLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 2048,
	};
	ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA));

	sdmmc_card_t *card = NULL;

	sdspi_device_config_t device_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
	device_cfg.gpio_cs = GPIO_SD_CS;
	device_cfg.host_id = host.slot;

	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = true,
		.max_files = 4,
		.allocation_unit_size = 16 * 1024};
	ESP_ERROR_CHECK(
		esp_vfs_fat_sdspi_mount("/sd", &host, &device_cfg, &mount_config, &card)
	);

	// Card has been initialized, print its properties
	sdmmc_card_print_info(stdout, card);
}

void list_files(const char *path) {
	ESP_LOGI(T, "Listing %s", path);
	DIR *dir = opendir(path);
	if (dir == NULL)
		return;

	while (true) {
		struct dirent *de = readdir(dir);
		if (!de)
			break;
		ESP_LOGI(T, "    %s", de->d_name);
	}

	closedir(dir);
}


// This handles websocket traffic, needs ESP-IDF > 4.2.x
static esp_err_t ws_handler(httpd_req_t *req) {
	if (req->method == HTTP_GET) {
		ESP_LOGI(T, "WS handshake");
		return ESP_OK;
	}

	// Copy the received payload into local buffer
	httpd_ws_frame_t wsf = {0};
	esp_err_t ret = httpd_ws_recv_frame(req, &wsf, 0);
	if (ret != ESP_OK)
		return ret;

	wsf.payload = malloc(wsf.len);
	if (!wsf.payload)
		return ESP_ERR_NO_MEM;

	ret = httpd_ws_recv_frame(req, &wsf, wsf.len);
	if (ret != ESP_OK) {
		free(wsf.payload);
		return ret;
	}

	// Process the payload
	if (wsf.type == HTTPD_WS_TYPE_TEXT && wsf.len > 0) {
		ESP_LOGI(T, "ws_callback(%c, %d)", wsf.payload[0], wsf.len);
		switch (wsf.payload[0]) {
		case 'a':
			// wsDumpRtc(req);  // read rolling log buffer in RTC memory
			break;

		case 'b':
			// read / write settings.json
			settings_ws_handler(req, &wsf.payload[1], wsf.len - 1);

			if (wsf.len > 2)
				reload_rgb_config();
			break;

		case 'r':
			esp_wifi_disconnect();
			set_brightness(0);
			vTaskDelay(100 / portTICK_PERIOD_MS);
			esp_restart();
			break;
		}
	}

	free(wsf.payload);
	return ESP_OK;
}


void app_main(void) {
	//------------------------------
	// init hardware
	//------------------------------
	// Power LED will be enabled by updateFrame loop if PD is not bad
	gpio_config_t cfg_o = {
		.pin_bit_mask = (1LL << GPIO_LED),
		.mode = GPIO_MODE_OUTPUT
	};
	ESP_ERROR_CHECK(gpio_config(&cfg_o));
	gpio_set_level(GPIO_LED, 0);

	// PD_BAD GPIO. If this is high we don't have juice. Run in low power mode
	// Wifi button will switch to hotspot mode
	gpio_config_t cfg_i = {
		.pin_bit_mask = (1LL << GPIO_PD_BAD) | (1LL << GPIO_WIFI),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE};
	ESP_ERROR_CHECK(gpio_config(&cfg_i));

	esp_log_level_set("*", ESP_LOG_INFO); // set all components to INFO level

	// report initial status
	ESP_LOGW(
		T, "reset reason: %d, heap: %ld, min_heap: %ld",
		rtc_get_reset_reason(0), esp_get_free_heap_size(),
		esp_get_minimum_free_heap_size()
	);

	// Mount spiffs for *.html and default_settings.json
	esp_vfs_spiffs_conf_t conf = {
		.base_path = "/spiffs",
		.partition_label = "filesys",
		.max_files = 4,
		.format_if_mount_failed = false};
	ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
	list_files("/spiffs");

	// Mount SD for animations, fonts and for settings.json
	mount_sd_card("/sd");
	list_files("/sd");

	// Load settings.json from SD card, try to create file if it doesn't exist
	set_settings_file("/sd/settings.json", "/spiffs/default_settings.json");

	// init I2S driven rgb - panel
	init_rgb();

	// init web-server
	initWifi();
	tryJsonConnect();
	startWebServer(ws_handler);

	// forward serial characters to web-console
	web_console_init();

	//------------------------------
	// Display test-patterns
	//------------------------------
	// only if enabled in json
	cJSON *jPanel = jGet(getSettings(), "panel");
	if (jGetB(jPanel, "test_pattern", true)) {
		ESP_LOGW(T, "RGB test-pattern mode!!!");
		set_brightness(MAX(0, jGetI(jPanel, "tp_brightness", 10)));
		tp_task(NULL);
	}

	//------------------------------
	// Open animation file on SD
	//------------------------------
	FILE *f = fopen(ANIMATION_FILE, "r");
	if (!f) {
		ESP_LOGE(
			T, "fopen(%s, rb) failed: %s", ANIMATION_FILE, strerror(errno)
		);
		ESP_LOGE(T, "Will not show animations!");
		vTaskDelete(NULL); // kill current task (== return;)
	}

	//-----------------------------------
	// Startup animated background layer
	//-----------------------------------
	// this one calls updateFrame and hence
	// sets the global maximum frame-rate
	xTaskCreatePinnedToCore(
		&aniBackgroundTask, "bck", 1750, NULL, 1, &t_backg, 1
	);

	//---------------------------------
	// Draw animations and clock layer
	//---------------------------------
	xTaskCreatePinnedToCore(&aniPinballTask, "pin", 5000, f, 0, &t_pinb, 0);

	vTaskDelete(NULL);
}
