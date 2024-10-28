#ifndef STATIC_WS_H
#define STATIC_WS_H

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void startWebServer();
void stopWebServer();

// override this in main!
void ws_callback(uint8_t *payload, unsigned len);

esp_err_t ws_send(uint8_t *payload, unsigned len);

#endif
