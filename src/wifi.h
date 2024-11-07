#ifndef WIFI_H
#define WIFI_H

#include "esp_http_server.h"

enum e_wifi {
	WIFI_NOT_CONNECTED,
	WIFI_SCANNING,
	WIFI_DPP_LISTENING,
	WIFI_CONNECTED,
	WIFI_AP_MODE,
};

extern int wifi_state;

// -------------------
//  wifi connection
// -------------------
void initWifi();

void tryJsonConnect();
void tryApMode();
void tryEasyConnect();


// -------------------
//  websocket logging
// -------------------
void web_console_init();

// dump the whole RTC buffer to the WS, oldest entries first.
// call this once the WS connection is open.
void wsDumpRtc(httpd_req_t *req);

void wsDisableLog();

#endif
