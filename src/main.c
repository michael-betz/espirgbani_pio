// The ESP32 Pinball Animation Clock

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/rtc.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "driver/sdmmc_host.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "esp_wifi.h"
#include "json_settings.h"
#include "static_ws.h"
#include "wifi.h"

#include "animations.h"
#include "common.h"
#include "font.h"
#include "frame_buffer.h"
#include "rgb_led_panel.h"
#include "shaders.h"

static const char *T = "MAIN";

TaskHandle_t t_backg = NULL;
TaskHandle_t t_pinb = NULL;

static esp_err_t mount_sd_card(const char *path) {
	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	host.max_freq_khz = 26000; // [kHz]

	spi_bus_config_t bus_cfg = {
		.mosi_io_num = GPIO_SD_MOSI,
		.miso_io_num = GPIO_SD_MISO,
		.sclk_io_num = GPIO_SD_CLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
	};
	ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_HOST)
	);
	gpio_pullup_en(GPIO_SD_MISO);
	gpio_pullup_en(GPIO_SD_CS);

	sdmmc_card_t *card = NULL;

	sdspi_device_config_t device_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
	device_cfg.gpio_cs = GPIO_SD_CS;
	device_cfg.host_id = host.slot;

	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = true,
		.max_files = 4,
	};

	esp_err_t ret = esp_vfs_fat_sdspi_mount(
		"/sd", &host, &device_cfg, &mount_config, &card
	);

	// sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	// sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
	// slot_config.width = 1;

	// slot_config.clk = GPIO_SD_CLK;
	// slot_config.cmd = GPIO_SD_MOSI;
	// slot_config.d0 = GPIO_SD_MISO;

	// // Enable internal pullups on enabled pins. The internal pullups
	// // are insufficient however, please make sure 10k external pullups are
	// // connected on the bus. This is for debug / example purpose only.
	// slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

	// ESP_LOGI(T, "Mounting filesystem");
	// ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config,
	// &mount_config, &card);

	// if (ret != ESP_OK) {
	//     if (ret == ESP_FAIL) {
	//         ESP_LOGE(T, "Failed to mount filesystem. ")
	//     } else {
	//         ESP_LOGE(T, "Failed to initialize the card (%s). ",
	//         esp_err_to_name(ret));
	//         // check_sd_card_pins(&config, pin_count);
	//     }
	//     return;
	// }

	if (ret == ESP_OK) {
		// Card has been initialized, print its properties
		sdmmc_card_print_info(stdout, card);
	} else {
		ESP_LOGE(
			T, "Failed to mount SD card. Continuing in test-pattern mode."
		);
	}
	return ret;
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

void init_log() {
	const cJSON *jLog = NULL;
	const cJSON *jLogs = jGet(getSettings(), "log_level");

	cJSON_ArrayForEach(jLog, jLogs) {
		if (!cJSON_IsNumber(jLog)) {
			ESP_LOGW(T, "log_level: ignoring %s (not an int)", jLog->string);
			continue;
		}
		esp_log_level_set(jLog->string, jLog->valueint);
	}
}

// This handles websocket traffic, needs ESP-IDF > 4.2.x
static esp_err_t ws_handler(httpd_req_t *req) {
	static char ret_buffer[64];
	int ret_len = -1;

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

	if (wsf.type == HTTPD_WS_TYPE_TEXT && wsf.len > 0) {
		// Process the payload
		ESP_LOGV(T, "ws_callback(%c, %d)", wsf.payload[0], wsf.len);
		switch (wsf.payload[0]) {
		case 'a':
			if (wsf.len > 1 && wsf.payload[1] == '1')
				// dump complete log buffer
				wsDumpRtc(req, true);
			else
				// dump updates only
				wsDumpRtc(req, false);
			break;

		case 'b':
			// read / write settings.json
			settings_ws_handler(req, &wsf.payload[1], wsf.len - 1);
			if (wsf.len > 1)
				init_log();
			break;

		case 'r':
			esp_wifi_disconnect();
			set_brightness(0);
			vTaskDelay(100 / portTICK_PERIOD_MS);
			esp_restart();
			break;

		case 'h':
			ret_len = snprintf(
				ret_buffer, sizeof(ret_buffer),
				"h{\"heap\": %ld, \"min_heap\": %ld}", esp_get_free_heap_size(),
				esp_get_minimum_free_heap_size()
			);
			break;
		}
	}
	free(wsf.payload);

	// Send reply if needed
	if (ret_len >= 0) {
		if (ret_len > sizeof(ret_buffer))
			ret_len = sizeof(ret_buffer);

		httpd_ws_frame_t ret_wsf = {0};
		ret_wsf.type = HTTPD_WS_TYPE_TEXT;
		ret_wsf.payload = (uint8_t *)ret_buffer;
		ret_wsf.len = ret_len;
		httpd_ws_send_frame(req, &ret_wsf);
	}
	return ESP_OK;
}

void app_main(void) {
	// forward serial characters to web-console
	web_console_init();

	//------------------------------
	// init hardware
	//------------------------------
	// Power LED will be enabled by updateFrame loop if PD is not bad
	gpio_config_t cfg_o = {
		.pin_bit_mask = (1LL << GPIO_LED), .mode = GPIO_MODE_OUTPUT
	};
	ESP_ERROR_CHECK(gpio_config(&cfg_o));
	gpio_set_level(GPIO_LED, 0);

	// PD_BAD GPIO. If this is high we don't have juice. Run in low power mode
	// Wifi button will switch to hotspot mode
	gpio_config_t cfg_i = {
#ifdef GPIO_PD_BAD
		.pin_bit_mask = (1LL << GPIO_PD_BAD) | (1LL << GPIO_WIFI),
#else
		.pin_bit_mask = (1LL << GPIO_WIFI),
#endif
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE
	};
	ESP_ERROR_CHECK(gpio_config(&cfg_i));

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
		.format_if_mount_failed = false
	};
	ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
	list_files("/spiffs");

	// Mount SD for animations, fonts and for settings.json
	esp_err_t ret = mount_sd_card("/sd");
	if (ret == ESP_OK) {
		list_files("/sd");
		// Load settings.json from SD card, try to create file if it doesn't
		// exist
		set_settings_file("/sd/settings.json", "/spiffs/default_settings.json");
	} else {
		// No SD card, load default settings and go to test-pattern mode
		set_settings_file("/spiffs/default_settings.json", NULL);
	}

	init_log();

	// init I2S driven rgb - panel
	init_rgb();

	// init web-server
	initWifi();
	tryJsonConnect();
	startWebServer(ws_handler);

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

	//-----------------------------------
	// Startup animated background layer
	//-----------------------------------
	// this one calls updateFrame and hence
	// sets the global maximum frame-rate
	xTaskCreatePinnedToCore(
		&aniBackgroundTask, "bck", 1024 * 4, NULL, 1, &t_backg, 1
	);

	//---------------------------------
	// Draw animations and clock layer
	//---------------------------------
	xTaskCreatePinnedToCore(
		&aniPinballTask, "pin", 1024 * 8, NULL, 0, &t_pinb, 0
	);

	vTaskDelete(NULL);
}
