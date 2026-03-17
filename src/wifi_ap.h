#pragma once

#include "esp_err.h"

// ============================================================================
// WiFi Access Point
// SSID: "MotorControl-XXXXXX" where XXXXXX = last 3 bytes of MAC (hex)
// IP:   192.168.4.1
// Open network (no password)
// ============================================================================

/**
 * Initialize NVS, netif, event loop, and start WiFi AP.
 * Call once from app_main before web_server_init.
 */
esp_err_t wifi_ap_init(void);
