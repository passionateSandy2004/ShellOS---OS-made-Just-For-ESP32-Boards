#include "wifi_driver.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRY      12
static const char *TAG_WIFI = "wifi_drv";

#define CONNECT_WAIT_MS     25000

static EventGroupHandle_t s_wifi_event_group = NULL;
static bool               s_wifi_initialized = false;
static bool               s_connected        = false;
static int                s_retry_count      = 0;
static char               s_ip[WIFI_IP_MAX_LEN] = {0};
/* Suppress esp_wifi_connect() from DISCONNECTED while we disconnect→set_config or scan */
static volatile bool      s_block_auto_reconnect = false;

/* ─────────────────────────────────────────
   Event handler
   ───────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    /*
     * Do NOT call esp_wifi_connect() on STA_START — esp_wifi_start() would begin
     * a connect before wifi_driver_connect() can esp_wifi_set_config(), causing
     * "sta is connecting, cannot set config". Connect only after set_config.
     */
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
        if (ev) {
            ESP_LOGW(TAG_WIFI, "STA disconnect, reason=%u", (unsigned)ev->reason);
        }
        s_connected = false;
        if (s_block_auto_reconnect) {
            return;
        }
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_connected   = true;
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ─────────────────────────────────────────
   Init — call once at boot
   ───────────────────────────────────────── */
void wifi_driver_init(void)
{
    if (s_wifi_initialized) {
        return;
    }

#if CONFIG_IDF_TARGET_ESP32C6
    /* Verbose wifi: lines go through console IO; keep headless boot reliable */
    esp_log_level_set("wifi", ESP_LOG_ERROR);
#endif

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    s_wifi_event_group = xEventGroupCreate();

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);

#if CONFIG_IDF_TARGET_ESP32C6
    /* 802.11ax can confuse some home routers with WiFi 6 clients; stick to b/g/n */
    esp_wifi_set_protocol(WIFI_IF_STA,
                          WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    wifi_country_t country = {
        .cc = "IN",
        .schan = 1,
        .nchan = 13,
        .max_tx_power = 20,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    esp_wifi_set_country(&country);
#endif

    esp_wifi_start();

    s_wifi_initialized = true;
}

/* ─────────────────────────────────────────
   Scan
   ───────────────────────────────────────── */
int wifi_driver_scan(wifi_scan_result_t *results, int max)
{
    if (!s_wifi_initialized) wifi_driver_init();

    /* Stop any existing connection attempt — block handler reconnect during scan */
    s_block_auto_reconnect = true;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(120));

    wifi_scan_config_t scan_cfg = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = true,
    };

    esp_err_t sr = esp_wifi_scan_start(&scan_cfg, true);  /* blocking */
    s_block_auto_reconnect = false;
    if (sr != ESP_OK) {
        return 0;
    }

    uint16_t found = max;
    wifi_ap_record_t *aps = calloc(max, sizeof(wifi_ap_record_t));
    if (!aps) return 0;

    esp_wifi_scan_get_ap_records(&found, aps);

    for (int i = 0; i < found; i++) {
        strncpy(results[i].ssid, (char *)aps[i].ssid, WIFI_SSID_MAX_LEN - 1);
        results[i].rssi     = aps[i].rssi;
        results[i].authmode = aps[i].authmode;
    }

    free(aps);
    return (int)found;
}

/* ─────────────────────────────────────────
   Connect
   ───────────────────────────────────────── */
bool wifi_driver_connect(const char *ssid, const char *password)
{
    if (!s_wifi_initialized) wifi_driver_init();

    /* Reset state */
    s_retry_count = 0;
    s_connected   = false;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid,     ssid,     sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    /* Do not set threshold.authmode to WPA2_PSK — some APs/PHY paths fail with
     * WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD (211); let IDF use defaults. */
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method  = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
#if CONFIG_IDF_TARGET_ESP32C6
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#endif

    /*
     * Disconnect first; DISCONNECTED must not call esp_wifi_connect() until
     * set_config — otherwise "sta is connecting, cannot set config".
     */
    s_block_auto_reconnect = true;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    s_block_auto_reconnect = false;
    if (err != ESP_OK) {
        return false;
    }
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(CONNECT_WAIT_MS));

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

/* ─────────────────────────────────────────
   Disconnect
   ───────────────────────────────────────── */
void wifi_driver_disconnect(void)
{
    s_block_auto_reconnect = true;
    esp_wifi_disconnect();
    s_connected = false;
    memset(s_ip, 0, sizeof(s_ip));
    /* Stay blocked until next wifi_driver_connect */
}

/* ─────────────────────────────────────────
   Get IP / Status
   ───────────────────────────────────────── */
bool wifi_driver_get_ip(char *ip_buf)
{
    if (!s_connected) return false;
    strncpy(ip_buf, s_ip, WIFI_IP_MAX_LEN - 1);
    return true;
}

bool wifi_driver_is_connected(void)
{
    return s_connected;
}
