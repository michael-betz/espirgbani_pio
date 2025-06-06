#ifndef JSON_SETTINGS_H
#define JSON_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"

#if defined(ESP_PLATFORM)
    #include <cJSON.h>
#else
    #include <cjson/cJSON.h>
#endif

// set the .json file with settings and a fall-back defaults_file
// which will be copied over if not NULL
// loads the settings file
void set_settings_file(const char *f_settings, const char *f_defaults);

// dump current settings file on websocket / re-write the settings file
void settings_ws_handler(httpd_req_t *req, uint8_t *data, size_t len);

// returns the cJSON object
cJSON *getSettings();

// Get item "string" from .json object. Returns NULL on error
#define jGet cJSON_GetObjectItemCaseSensitive

// return string from .json or default-value on error
const char *jGetS(const cJSON *json, const char *name, const char *default_val);

// return integer from .json or default-value on error
int jGetI(cJSON *json, const char *name, int default_val);

// return double from .json or default-value on error
double jGetD(cJSON *json, const char *name, double default_val);

// return bool from .json or default-value on error
bool jGetB(cJSON *json, const char *name, bool default_val);

// Init log levels from an dict with name `log_level` in the config
void init_log_levels();

#endif
