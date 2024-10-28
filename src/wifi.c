#include <string.h>
#include "json_settings.h"
#include "lwip/apps/sntp.h"
#include "lwip/sockets.h"
// #include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "static_ws.h"
#include "wifi.h"

#include "esp_dpp.h"
#include "qrcode.h"

#define E(x) ESP_ERROR_CHECK_WITHOUT_ABORT(x)

static const char *T = "WIFI";

int wifi_state = WIFI_NOT_CONNECTED;

// for sending the log output over UDP
// receive it on the target machine with: netcat -lukp 1234
static int dbgSock = -1;
static struct sockaddr_in g_servaddr;
vprintf_like_t log_original = NULL;

static int udpDebugPrintf(const char *format, va_list arg)
{
	static char charBuffer[255];

	if (log_original)
		log_original(format, arg);

	if (dbgSock < 0)
		return 0;

	int charLen = vsnprintf(charBuffer, sizeof(charBuffer), format, arg);
	if (charLen <= 0)
		return 0;

	int ret = sendto(
		dbgSock,
		charBuffer,
		charLen,
		0,
		(struct sockaddr *)&g_servaddr,
		sizeof(g_servaddr)
	);

	if (ret < 0)
		return 0;

	return ret;
}

static void udp_debug_init()
{
	cJSON *s = getSettings();

	if (dbgSock >= 0 || jGetB(s, "log_disable", true))
		return;

	// put the host's address / port into the server address structure
	memset(&g_servaddr, 0, sizeof(g_servaddr));
	g_servaddr.sin_family = AF_INET;
	g_servaddr.sin_port = htons(jGetI(s, "log_port", 1234));
	g_servaddr.sin_addr.s_addr = inet_addr(jGetS(s, "log_ip", "255.255.255.255"));
	ESP_LOGI(T, "UDP log --> %s:%d", inet_ntoa(g_servaddr.sin_addr), ntohs(g_servaddr.sin_port));

	if ((dbgSock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		ESP_LOGE(T, "Failed to create UDP socket: %s", strerror(errno));
		return;
	}

	ESP_LOGI(T, "Enabling UDP broadcast option");
	int enabled = 1;
	setsockopt(dbgSock, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));

	if (!log_original) {
		ESP_LOGW(T, "Installed UDP logger");
		vprintf_like_t tmp = esp_log_set_vprintf(udpDebugPrintf);
		log_original = tmp;
	}
}


// go through wifi scan results and look for the first known wifi
// if the wifi is known, meaning it is configured in the .json, connect to it
// if no known wifi is found, start the AP
static void scan_done(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	uint16_t n = 24;
	static wifi_ap_record_t ap_info[24];

	E(esp_wifi_scan_get_ap_records(&n, ap_info));

	if (n < 1) {
		ESP_LOGE(T, "wifi scan failed");
		return;
	}

	cJSON *jWifi, *jWifis = jGet(getSettings(), "wifis");
	if (!jWifis) {
		ESP_LOGE(T, "no wifis defined in .json");
		return;
	}

	// Go through all found APs, use ssid as key and try to get item from json
	ESP_LOGI(T, "Wifis I can see:");
	for (unsigned i = 0; i < n; i++) {
		ESP_LOGI(T, "    %s (%d, %d dBm)", ap_info[i].ssid, ap_info[i].primary, ap_info[i].rssi);
	}

	for (unsigned i = 0; i < n; i++) {
		const char *ssid = (const char *)ap_info[i].ssid;
		jWifi = jGet(jWifis, ssid);
		if (!cJSON_IsString(jWifi))
			continue;

		// Found a known good WIFI, connect to it ...
		ESP_LOGI(T, "Looks familiar: %s", ssid);
		char *pw = jWifi->valuestring;
		wifi_config_t cfg;
		memset(&cfg, 0, sizeof(cfg));
		strncpy((char*)cfg.sta.ssid, ssid, 31);
		strncpy((char*)cfg.sta.password, pw, 63);
		cfg.sta.scan_method = WIFI_FAST_SCAN;
		cfg.sta.bssid_set = true;
		memcpy(cfg.sta.bssid, ap_info[i].bssid, 6);
		cfg.sta.channel = ap_info[i].primary;
		cfg.sta.pmf_cfg.capable = true;

		E(esp_wifi_set_mode(WIFI_MODE_STA));
		E(esp_wifi_set_config(ESP_IF_WIFI_STA, &cfg));
		E(esp_wifi_connect());
		E(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
		return;
	}

	ESP_LOGW(T, "no known Wifi found");
	wifi_state = WIFI_NOT_CONNECTED;
}

static void got_ip(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	udp_debug_init();

	ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
	ESP_LOGI(T, "Got ip " IPSTR, IP2STR(&event->ip_info.ip));

	// trigger time sync
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	const char *ntp_str = jGetS(getSettings(), "ntp_host", "pool.ntp.org");
	sntp_setservername(0, ntp_str);
	sntp_init();

	wifi_state = WIFI_CONNECTED;
}

static void got_discon(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	dbgSock = -1;

	ESP_LOGW(T, "got disconnected :(");
	if (event_data) {
		wifi_event_sta_disconnected_t *ed = (wifi_event_sta_disconnected_t*)event_data;
		ESP_LOGW(T, "reason: %d", ed->reason);
	}

	sntp_stop();
	wifi_state = WIFI_NOT_CONNECTED;
}

static void dpp_enrollee_event_cb(esp_supp_dpp_event_t event, void *data)
{
	static int retries = 8;

	switch (event) {
	case ESP_SUPP_DPP_URI_READY:
		retries = 8;
		if (data != NULL) {
			esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
			ESP_LOGI(T, "Scan below QR Code to configure the enrollee:\n");
			esp_qrcode_generate(&cfg, (const char *)data);
		}
		break;

	case ESP_SUPP_DPP_CFG_RECVD:
		retries = 8;
		wifi_config_t s_dpp_wifi_config;
		memcpy(&s_dpp_wifi_config, data, sizeof(s_dpp_wifi_config));
		ESP_LOGI(T, "DPP Authentication successful, ssid: %s", s_dpp_wifi_config.sta.ssid);
		// TODO store ssid and pw in .json config, then call tryJsonConnect()
		break;

	case ESP_SUPP_DPP_FAIL:
		ESP_LOGW(T, "DPP Auth failed (Reason: %s)", esp_err_to_name((int)data));
		if (retries-- > 0) {
			ESP_LOGI(T, "retry ...");
 			E(esp_supp_dpp_start_listen());
		}
		break;

	default:
		break;
	}
}

static wifi_config_t wifi_ap_config = {
	.ap = {
		.channel = 6,
		.max_connection = 3,
		.authmode = WIFI_AUTH_OPEN,
		.pmf_cfg = {
			.required = false,
		},
	},
};

void initWifi()
{
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		E(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	E(ret);

	E(esp_netif_init());
	E(esp_event_loop_create_default());

	// register some async callbacks
	E(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &scan_done, NULL));
	E(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip, NULL));
	E(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &got_discon, NULL));
	E(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_STOP, &got_discon, NULL));

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	E(esp_wifi_init(&cfg));

	// Initialize default station as network interface instance (esp-netif)
	esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
	assert(sta_netif);
	esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
	assert(ap_netif);

	// Init DNS and mDNS
	const char *hostname = jGetS(getSettings(), "hostname", WIFI_HOST_NAME);
	E(esp_netif_set_hostname(sta_netif, hostname));
	E(mdns_init());
	E(mdns_hostname_set(hostname));

	// Set the timezone
	const char *tz_str = jGetS(getSettings(), "timezone", "PST8PDT");
	ESP_LOGI(T, "Setting timezone to TZ = %s", tz_str);
	setenv("TZ", tz_str, 1);
	tzset();

	// Setup AP mode (may be used if no known wifi is found)
	int l = strlen(hostname);
	if (l > sizeof(wifi_ap_config.ap.ssid))
		l = sizeof(wifi_ap_config.ap.ssid);
	wifi_ap_config.ap.ssid_len = l;
	memcpy(wifi_ap_config.ap.ssid, hostname, l);

	E(esp_wifi_start());

	startWebServer();
}

void tryJsonConnect()
{
	// Initialize and start WiFi scan
	E(esp_wifi_set_mode(WIFI_MODE_STA));
	E(esp_wifi_scan_start(NULL, false));
	// fires SYSTEM_EVENT_SCAN_DONE when done, calls scan_done() ...
	wifi_state = WIFI_SCANNING;
}

void tryApMode()
{
	E(esp_wifi_set_mode(WIFI_MODE_AP));
	E(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
	ESP_LOGI(T, "started AP mode. SSID: %s", WIFI_HOST_NAME);
	wifi_state = WIFI_AP_MODE;
}

void tryEasyConnect()
{
	// nice idea but doesn't work
	E(esp_wifi_set_mode(WIFI_MODE_STA));
	E(esp_supp_dpp_init(dpp_enrollee_event_cb));
	E(esp_supp_dpp_bootstrap_gen(
		"6",  // DPP Bootstrapping listen channels separated by commas.
		DPP_BOOTSTRAP_QR_CODE,
		NULL,  // Private key string for DPP Bootstrapping in PEM format.
		NULL  // Additional ancillary information to be included in QR Code.
	));

	E(esp_supp_dpp_start_listen());
	ESP_LOGI(T, "Started listening for DPP Authentication");
	wifi_state = WIFI_DPP_LISTENING;
}
