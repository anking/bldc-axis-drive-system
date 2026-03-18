#include "wifi_ap.h"

#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_mgr";

#define AP_CHANNEL      1
#define AP_MAX_CONN     4
#define NVS_NAMESPACE   "wifi_cfg"
#define NVS_KEY_SSID    "sta_ssid"
#define NVS_KEY_PASS    "sta_pass"
#define STA_MAX_RETRY   5
#define SCAN_MAX_APS    20

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static wifi_sta_status_t s_sta_status = WIFI_STA_DISCONNECTED;
static char s_sta_ip[20]   = "";
static char s_sta_ssid[33] = "";
static char s_hostname[32] = "";
static char s_ap_ssid[32]  = "";

static int  s_retry_count = 0;
static bool s_scan_done   = false;

static wifi_ap_record_t s_scan_results[SCAN_MAX_APS];
static uint16_t s_scan_count = 0;

static esp_netif_t *s_sta_netif = NULL;

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------
static esp_err_t nvs_save_sta_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, pass);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "STA credentials saved to NVS");
    return ESP_OK;
}

static esp_err_t nvs_load_sta_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) return ret;

    ret = nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len);
    if (ret != ESP_OK) { nvs_close(h); return ret; }

    ret = nvs_get_str(h, NVS_KEY_PASS, pass, &pass_len);
    if (ret != ESP_OK) { nvs_close(h); return ret; }

    nvs_close(h);
    return ESP_OK;
}

static esp_err_t nvs_clear_sta_creds(void)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    nvs_erase_key(h, NVS_KEY_SSID);
    nvs_erase_key(h, NVS_KEY_PASS);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "STA credentials cleared from NVS");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Event handler
// ---------------------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *ev = (wifi_event_ap_staconnected_t *)data;
            ESP_LOGI(TAG, "AP: station connected (AID=%d)", ev->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *ev = (wifi_event_ap_stadisconnected_t *)data;
            ESP_LOGI(TAG, "AP: station disconnected (AID=%d)", ev->aid);
            break;
        }
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA: started");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "STA: connected to AP");
            s_sta_status = WIFI_STA_CONNECTING;  // waiting for IP
            s_retry_count = 0;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_sta_status == WIFI_STA_CONNECTING || s_sta_status == WIFI_STA_CONNECTED) {
                if (s_retry_count < STA_MAX_RETRY) {
                    s_retry_count++;
                    ESP_LOGI(TAG, "STA: reconnecting (attempt %d/%d)", s_retry_count, STA_MAX_RETRY);
                    esp_wifi_connect();
                    s_sta_status = WIFI_STA_CONNECTING;
                } else {
                    ESP_LOGW(TAG, "STA: connection failed after %d retries", STA_MAX_RETRY);
                    s_sta_status = WIFI_STA_FAILED;
                    s_sta_ip[0] = '\0';
                }
            } else {
                s_sta_status = WIFI_STA_DISCONNECTED;
                s_sta_ip[0] = '\0';
            }
            break;
        case WIFI_EVENT_SCAN_DONE:
            s_scan_count = SCAN_MAX_APS;
            esp_wifi_scan_get_ap_records(&s_scan_count, s_scan_results);
            s_scan_done = true;
            ESP_LOGI(TAG, "Scan complete: %d networks found", s_scan_count);
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        s_sta_status = WIFI_STA_CONNECTED;
        s_retry_count = 0;
        ESP_LOGI(TAG, "STA: got IP %s", s_sta_ip);

        // Save credentials on successful connection
        wifi_config_t cfg;
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
            nvs_save_sta_creds((const char *)cfg.sta.ssid, (const char *)cfg.sta.password);
            strlcpy(s_sta_ssid, (const char *)cfg.sta.ssid, sizeof(s_sta_ssid));
        }
    }
}

// ---------------------------------------------------------------------------
// Internal: start STA connection
// ---------------------------------------------------------------------------
static esp_err_t sta_connect_internal(const char *ssid, const char *password)
{
    wifi_config_t sta_cfg = {};
    strlcpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid));
    if (password && password[0]) {
        strlcpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password));
    }
    sta_cfg.sta.threshold.authmode = (password && password[0])
        ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

    s_sta_status = WIFI_STA_CONNECTING;
    s_retry_count = 0;
    strlcpy(s_sta_ssid, ssid, sizeof(s_sta_ssid));

    ESP_LOGI(TAG, "STA: connecting to \"%s\"", ssid);
    return esp_wifi_connect();
}

// ---------------------------------------------------------------------------
// Public: init
// ---------------------------------------------------------------------------
esp_err_t wifi_ap_init(void)
{
    // --- NVS (required by WiFi) ---
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- Network interface + event loop ---
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();

    // --- WiFi init ---
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // --- Build SSID / hostname from MAC ---
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "MotorControl-%02X%02X%02X",
             mac[3], mac[4], mac[5]);
    snprintf(s_hostname, sizeof(s_hostname), "motor-control-%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    // --- Configure AP ---
    wifi_config_t ap_cfg = {
        .ap = {
            .channel        = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode       = WIFI_AUTH_OPEN,
        },
    };
    strlcpy((char *)ap_cfg.ap.ssid, s_ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(s_ap_ssid);

    // --- Start in AP+STA mode ---
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP+STA started: AP SSID=\"%s\"  AP IP=192.168.4.1", s_ap_ssid);

    // --- mDNS ---
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set(s_hostname);
    mdns_instance_name_set("BLDC Motor Controller");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: %s.local", s_hostname);

    // --- Auto-connect to saved STA network ---
    char saved_ssid[33] = "";
    char saved_pass[65] = "";
    if (nvs_load_sta_creds(saved_ssid, sizeof(saved_ssid),
                           saved_pass, sizeof(saved_pass)) == ESP_OK
        && saved_ssid[0] != '\0') {
        ESP_LOGI(TAG, "Found saved STA credentials for \"%s\", connecting...", saved_ssid);
        sta_connect_internal(saved_ssid, saved_pass);
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public: scan
// ---------------------------------------------------------------------------
esp_err_t wifi_start_scan(void)
{
    s_scan_done = false;
    s_scan_count = 0;

    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, false);  // non-blocking
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

bool wifi_scan_is_done(void)
{
    return s_scan_done;
}

int wifi_get_scan_results_json(char *buf, size_t buf_size)
{
    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "[");

    for (int i = 0; i < s_scan_count && pos < (int)buf_size - 100; i++) {
        if (i > 0) pos += snprintf(buf + pos, buf_size - pos, ",");

        const char *auth = "OPEN";
        switch (s_scan_results[i].authmode) {
            case WIFI_AUTH_WEP:         auth = "WEP"; break;
            case WIFI_AUTH_WPA_PSK:     auth = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK:    auth = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: auth = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK:    auth = "WPA3"; break;
            case WIFI_AUTH_WPA2_WPA3_PSK: auth = "WPA2/WPA3"; break;
            case WIFI_AUTH_OPEN:        auth = "OPEN"; break;
            default:                    auth = "OTHER"; break;
        }

        pos += snprintf(buf + pos, buf_size - pos,
            "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\",\"ch\":%d}",
            (const char *)s_scan_results[i].ssid,
            s_scan_results[i].rssi,
            auth,
            s_scan_results[i].primary);
    }

    pos += snprintf(buf + pos, buf_size - pos, "]");
    return pos;
}

// ---------------------------------------------------------------------------
// Public: connect / disconnect
// ---------------------------------------------------------------------------
esp_err_t wifi_sta_connect(const char *ssid, const char *password)
{
    // Disconnect first if already connected
    if (s_sta_status == WIFI_STA_CONNECTED || s_sta_status == WIFI_STA_CONNECTING) {
        esp_wifi_disconnect();
        s_sta_status = WIFI_STA_DISCONNECTED;
        s_sta_ip[0] = '\0';
    }

    return sta_connect_internal(ssid, password ? password : "");
}

esp_err_t wifi_sta_disconnect_and_clear(void)
{
    esp_wifi_disconnect();
    s_sta_status = WIFI_STA_DISCONNECTED;
    s_sta_ip[0] = '\0';
    s_sta_ssid[0] = '\0';
    nvs_clear_sta_creds();
    ESP_LOGI(TAG, "STA: disconnected and credentials cleared");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public: getters
// ---------------------------------------------------------------------------
wifi_sta_status_t wifi_sta_get_status(void)
{
    return s_sta_status;
}

const char *wifi_sta_get_ip(void)
{
    return s_sta_ip;
}

const char *wifi_sta_get_ssid(void)
{
    return s_sta_ssid;
}

const char *wifi_get_hostname(void)
{
    return s_hostname;
}
