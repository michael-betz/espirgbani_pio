#include "json_settings.h"
#include "esp_log.h"
#include "static_ws.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *T = "JSON_S";

static cJSON *g_settings = NULL;
static const char *g_settings_file = NULL;

char *readFileDyn(const char *file_name, int *file_size) {
	// opens the file file_name and returns it as dynamically allocated char*
	// if file_size is not NULL, copies the number of bytes read there
	// dont forget to call free() on the char* result
	if (file_name == NULL)
		return NULL;

	FILE *f = fopen(file_name, "rb");
	if (!f) {
		ESP_LOGE(T, "fopen(%s, rb) failed: %s", file_name, strerror(errno));
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	int fsize = ftell(f);
	fseek(f, 0, SEEK_SET); // same as rewind(f);
	ESP_LOGI(T, "loading %s, fsize: %d", file_name, fsize);
	char *string = (char *)malloc(fsize + 1);
	if (!string) {
		ESP_LOGE(T, "malloc(%d) failed: %s", fsize + 1, strerror(errno));
		assert(false);
	}
	fread(string, fsize, 1, f);
	fclose(f);
	string[fsize] = 0;
	if (file_size)
		*file_size = fsize;
	return string;
}

cJSON *readJsonDyn(const char *file_name) {
	// opens the json file `file_name` and returns it as cJSON*
	// don't forget to call cJSON_Delete() on it
	cJSON *root;
	char *txtData = NULL;

	// try to read settings.json from SD card
	txtData = readFileDyn(file_name, NULL);
	if (txtData == NULL)
		return NULL;

	// load txtData as .json
	if (!(root = cJSON_Parse(txtData))) {
		ESP_LOGE(T, "JSON Error in %s", file_name);
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL) {
			ESP_LOGE(T, "Error before: %s", error_ptr);
		}
	}

	free(txtData);
	return root;
}

void settings_ws_handler(uint8_t *data, size_t len) {
	// if data is given, it will overwrite the settings file
	// then it will read the settings file and send it over the websocket
	if (data != NULL && len > 1) {
		data[len] = 0;
		FILE *dest = fopen(g_settings_file, "wb");
		if (dest) {
			fputs((const char *)data, dest);
			fclose(dest);
			dest = NULL;
			ESP_LOGI(T, "re-wrote %s", g_settings_file);

			set_settings_file(NULL, NULL);
		} else {
			ESP_LOGE(
				T, "fopen(%s, wb) failed: %s", g_settings_file, strerror(errno)
			);
		}
	}

	// Send content of currently loaded settings file over websocket
	char *file_data = cJSON_Print(g_settings);
	if (file_data) {
		ws_send((uint8_t *)file_data, strlen(file_data));
		cJSON_free(file_data);
	}
}

void set_settings_file(const char *f_settings, const char *f_defaults) {
	if (f_settings != NULL)
		g_settings_file = f_settings;

	if (g_settings != NULL)
		cJSON_free(g_settings);

	g_settings = readJsonDyn(g_settings_file);

	if (f_defaults && (g_settings == NULL)) {
		char buf[32];
		size_t size;
		ESP_LOGW(T, "writing default-settings to %s", g_settings_file);
		FILE *source = fopen(f_defaults, "rb");
		FILE *dest = fopen(g_settings_file, "wb");
		if (source && dest) {
			while ((size = fread(buf, 1, sizeof(buf), source)))
				fwrite(buf, 1, size, dest);
		} else {
			ESP_LOGE(
				T, "could not copy %s to %s: %s", f_defaults, g_settings_file,
				strerror(errno)
			);
		}

		if (source)
			fclose(source);

		if (dest)
			fclose(dest);

		g_settings = readJsonDyn(g_settings_file);
	}
}

cJSON *getSettings() { return g_settings; }

// return string from .json or default-value on error
const char *
jGetS(const cJSON *json, const char *name, const char *default_val) {
	const cJSON *j = cJSON_GetObjectItemCaseSensitive(json, name);
	if (!cJSON_IsString(j)) {
		ESP_LOGW(
			T, "%s is not a string, falling back to %s", name, default_val
		);
		return default_val;
	}
	return j->valuestring;
}

// return integer from .json or default-value on error
int jGetI(cJSON *json, const char *name, int default_val) {
	const cJSON *j = cJSON_GetObjectItemCaseSensitive(json, name);
	if (!cJSON_IsNumber(j)) {
		ESP_LOGW(
			T, "%s is not a number, falling back to %d", name, default_val
		);
		return default_val;
	}
	return j->valueint;
}

// return double from .json or default-value on error
double jGetD(cJSON *json, const char *name, double default_val) {
	const cJSON *j = cJSON_GetObjectItemCaseSensitive(json, name);
	if (!cJSON_IsNumber(j)) {
		ESP_LOGW(
			T, "%s is not a number, falling back to %f", name, default_val
		);
		return default_val;
	}
	return j->valuedouble;
}

// return bool from .json or default-value on error
bool jGetB(cJSON *json, const char *name, bool default_val) {
	const cJSON *j = cJSON_GetObjectItemCaseSensitive(json, name);
	if (!cJSON_IsBool(j)) {
		ESP_LOGW(
			T, "%s is not a bool, falling back to %s", name,
			default_val ? "true" : "false"
		);
		return default_val;
	}
	return cJSON_IsTrue(j);
}
