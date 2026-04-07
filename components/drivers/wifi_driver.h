#ifndef WIFI_DRIVER_H
#define WIFI_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

/* Max results from a wifi scan */
#define WIFI_MAX_SCAN_RESULTS  15
#define WIFI_SSID_MAX_LEN      33
#define WIFI_PASS_MAX_LEN      65
#define WIFI_IP_MAX_LEN        16

typedef struct {
    char     ssid[WIFI_SSID_MAX_LEN];
    int8_t   rssi;
    uint8_t  authmode;
} wifi_scan_result_t;

/* Init the WiFi system (call once at boot) */
void wifi_driver_init(void);

/* Scan nearby networks — returns count found */
int  wifi_driver_scan(wifi_scan_result_t *results, int max);

/* Connect to a network — returns true on success */
bool wifi_driver_connect(const char *ssid, const char *password);

/* Disconnect */
void wifi_driver_disconnect(void);

/* Get current IP as string — returns false if not connected */
bool wifi_driver_get_ip(char *ip_buf);

/* Check if connected */
bool wifi_driver_is_connected(void);

#endif // WIFI_DRIVER_H
