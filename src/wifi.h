#ifndef WIFI_H
#define WIFI_H

#define WIFI_HOST_NAME "esp_crt"

enum e_wifi {
    WIFI_NOT_CONNECTED,
    WIFI_SCANNING,
    WIFI_DPP_LISTENING,
    WIFI_CONNECTED,
    WIFI_AP_MODE,
};

extern int wifi_state;
// extern char *qr_code;
// extern unsigned qr_code_w;

void initWifi();

void tryJsonConnect();
void tryApMode();
void tryEasyConnect();

#endif
