#pragma once

#include <stdio.h>

#define ESP_LOGE(T, format, ...) printf(format "\n", ##__VA_ARGS__)
#define ESP_LOGW(T, format, ...) printf(format "\n", ##__VA_ARGS__)
#define ESP_LOGI(T, format, ...) printf(format "\n", ##__VA_ARGS__)
#define ESP_LOGD(T, format, ...) printf(format "\n", ##__VA_ARGS__)
#define ESP_LOGV(T, format, ...) printf(format "\n", ##__VA_ARGS__)
