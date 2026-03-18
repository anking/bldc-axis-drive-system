#pragma once

#include "esp_err.h"
#include <stdbool.h>

// ============================================================================
// WiFi AP+STA Manager
// AP:  "MotorControl-XXXXXX" where XXXXXX = last 3 bytes of MAC (hex)
//      IP: 192.168.4.1, open network
// STA: Connects to saved network from NVS, credentials persist across reboots
// mDNS: motor-control-XXXXXX.local
// ============================================================================

// WiFi STA connection status
typedef enum {
    WIFI_STA_DISCONNECTED = 0,
    WIFI_STA_CONNECTING,
    WIFI_STA_CONNECTED,
    WIFI_STA_FAILED,
} wifi_sta_status_t;

/**
 * Initialize NVS, netif, event loop, and start WiFi in AP+STA mode.
 * If saved STA credentials exist in NVS, auto-connects to that network.
 * Starts mDNS responder at motor-control-XXXXXX.local.
 * Call once from app_main before web_server_init.
 */
esp_err_t wifi_ap_init(void);

/**
 * Start a WiFi scan. Results available via wifi_get_scan_results().
 * Non-blocking — polls wifi_scan_is_done() to check completion.
 */
esp_err_t wifi_start_scan(void);

/**
 * Check if scan is complete.
 */
bool wifi_scan_is_done(void);

/**
 * Get scan results as a JSON string written into buf.
 * Returns number of bytes written (excluding null), or -1 on error.
 */
int wifi_get_scan_results_json(char *buf, size_t buf_size);

/**
 * Connect to a WiFi network. Saves credentials to NVS on success.
 */
esp_err_t wifi_sta_connect(const char *ssid, const char *password);

/**
 * Disconnect from STA network and clear saved credentials.
 */
esp_err_t wifi_sta_disconnect_and_clear(void);

/**
 * Get current STA connection status.
 */
wifi_sta_status_t wifi_sta_get_status(void);

/**
 * Get STA IP address as string. Returns "" if not connected.
 */
const char *wifi_sta_get_ip(void);

/**
 * Get connected SSID. Returns "" if not connected.
 */
const char *wifi_sta_get_ssid(void);

/**
 * Get the mDNS hostname (e.g., "motor-control-A1B2C3").
 */
const char *wifi_get_hostname(void);
