#include "web_server.h"
#include "web_dashboard.h"
#include "pin_config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "web_srv";

static drive_controller_t *s_dc = NULL;
static SemaphoreHandle_t   s_cmd_mutex = NULL;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Parse "motor" query param → 0, 1, or -1 (all)
static int parse_motor_param(httpd_req_t *req)
{
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK)
        return -1;

    char val[8];
    if (httpd_query_key_value(buf, "motor", val, sizeof(val)) != ESP_OK)
        return -1;

    if (strcmp(val, "all") == 0) return -1;
    int m = atoi(val);
    if (m >= 0 && m < MOTOR_COUNT) return m;
    return -1;
}

// Parse a float query param by key
static float parse_float_param(httpd_req_t *req, const char *key, float def)
{
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK)
        return def;

    char val[16];
    if (httpd_query_key_value(buf, key, val, sizeof(val)) != ESP_OK)
        return def;

    return strtof(val, NULL);
}

// Parse a string query param by key
static bool parse_str_param(httpd_req_t *req, const char *key, char *out, size_t len)
{
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK)
        return false;
    return httpd_query_key_value(buf, key, out, len) == ESP_OK;
}

// Send a short JSON OK response
static esp_err_t send_ok(httpd_req_t *req, const char *msg)
{
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"msg\":\"%s\"}", msg);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// GET / — Dashboard HTML
// ---------------------------------------------------------------------------
static esp_err_t handler_dashboard(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, DASHBOARD_HTML, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// GET /api/status — JSON status
// ---------------------------------------------------------------------------
static esp_err_t handler_status(httpd_req_t *req)
{
    // Build JSON into a stack buffer
    char json[768];
    int pos = 0;

    int64_t uptime_ms = esp_timer_get_time() / 1000;
    uint32_t heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

    pos += snprintf(json + pos, sizeof(json) - pos,
        "{\"uptime_ms\":%lld,\"heap_free\":%lu,\"running\":%s,"
        "\"motor_count\":%d,\"motors\":[",
        (long long)uptime_ms,
        (unsigned long)heap,
        s_dc->running ? "true" : "false",
        MOTOR_COUNT);

    for (int i = 0; i < MOTOR_COUNT; i++) {
        drive_axis_t *ax = &s_dc->axes[i];
        if (i > 0) pos += snprintf(json + pos, sizeof(json) - pos, ",");
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"id\":%d,"
            "\"rpm\":%.1f,"
            "\"target_rpm\":%.1f,"
            "\"duty_pct\":%d,"
            "\"dir\":\"%s\","
            "\"stalled\":%s,"
            "\"braking\":%s,"
            "\"coasting\":%s}",
            i,
            tacho_get_rpm(&ax->tacho),
            ax->target_rpm,
            ax->motor.duty_pct,
            ax->motor.direction == MOTOR_DIR_FORWARD ? "fwd" : "rev",
            ax->stalled ? "true" : "false",
            ax->motor.braking ? "true" : "false",
            ax->motor.coasting ? "true" : "false");
    }

    pos += snprintf(json + pos, sizeof(json) - pos, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, pos);
}

// ---------------------------------------------------------------------------
// GET /api/set?rpm=<float>&motor=<0|1|all>
// ---------------------------------------------------------------------------
static esp_err_t handler_set_rpm(httpd_req_t *req)
{
    float rpm = parse_float_param(req, "rpm", -1.0f);
    int motor = parse_motor_param(req);

    if (rpm < 0.0f) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"msg\":\"missing rpm param\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    if (motor < 0) {
        drive_controller_set_rpm(s_dc, rpm);
        ESP_LOGI(TAG, "Set ALL RPM=%.1f", rpm);
    } else {
        s_dc->axes[motor].target_rpm = rpm;
        ESP_LOGI(TAG, "Set M%d RPM=%.1f", motor, rpm);
    }
    xSemaphoreGive(s_cmd_mutex);

    return send_ok(req, "rpm set");
}

// ---------------------------------------------------------------------------
// GET /api/dir?dir=<fwd|rev>&motor=<0|1|all>
// ---------------------------------------------------------------------------
static esp_err_t handler_set_dir(httpd_req_t *req)
{
    char dir_str[8];
    if (!parse_str_param(req, "dir", dir_str, sizeof(dir_str))) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"msg\":\"missing dir param\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    motor_dir_t dir = (strcmp(dir_str, "rev") == 0)
                      ? MOTOR_DIR_REVERSE : MOTOR_DIR_FORWARD;
    int motor = parse_motor_param(req);

    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    if (motor < 0) {
        drive_controller_set_direction(s_dc, dir);
        ESP_LOGI(TAG, "Set ALL dir=%s", dir_str);
    } else {
        motor_driver_set_direction(&s_dc->axes[motor].motor, dir);
        ESP_LOGI(TAG, "Set M%d dir=%s", motor, dir_str);
    }
    xSemaphoreGive(s_cmd_mutex);

    return send_ok(req, "direction set");
}

// ---------------------------------------------------------------------------
// GET /api/start
// ---------------------------------------------------------------------------
static esp_err_t handler_start(httpd_req_t *req)
{
    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    drive_controller_start(s_dc);
    xSemaphoreGive(s_cmd_mutex);
    ESP_LOGI(TAG, "Control loop started via web");
    return send_ok(req, "started");
}

// ---------------------------------------------------------------------------
// GET /api/stop
// ---------------------------------------------------------------------------
static esp_err_t handler_stop(httpd_req_t *req)
{
    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    drive_controller_emergency_stop(s_dc);
    xSemaphoreGive(s_cmd_mutex);
    ESP_LOGW(TAG, "Emergency stop via web");
    return send_ok(req, "stopped");
}

// ---------------------------------------------------------------------------
// GET /api/brake?motor=<0|1|all>
// ---------------------------------------------------------------------------
static esp_err_t handler_brake(httpd_req_t *req)
{
    int motor = parse_motor_param(req);

    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    if (motor < 0) {
        for (int i = 0; i < MOTOR_COUNT; i++)
            motor_driver_brake(&s_dc->axes[i].motor);
    } else {
        motor_driver_brake(&s_dc->axes[motor].motor);
    }
    xSemaphoreGive(s_cmd_mutex);

    ESP_LOGI(TAG, "Brake engaged (motor=%d)", motor);
    return send_ok(req, "brake engaged");
}

// ---------------------------------------------------------------------------
// GET /api/coast?motor=<0|1|all>
// ---------------------------------------------------------------------------
static esp_err_t handler_coast(httpd_req_t *req)
{
    int motor = parse_motor_param(req);

    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    if (motor < 0) {
        for (int i = 0; i < MOTOR_COUNT; i++)
            motor_driver_coast(&s_dc->axes[i].motor);
    } else {
        motor_driver_coast(&s_dc->axes[motor].motor);
    }
    xSemaphoreGive(s_cmd_mutex);

    ESP_LOGI(TAG, "Coast mode (motor=%d)", motor);
    return send_ok(req, "coast mode");
}

// ---------------------------------------------------------------------------
// Server init
// ---------------------------------------------------------------------------

esp_err_t web_server_init(drive_controller_t *dc)
{
    s_dc = dc;
    s_cmd_mutex = xSemaphoreCreateMutex();
    if (!s_cmd_mutex) {
        ESP_LOGE(TAG, "Failed to create command mutex");
        return ESP_FAIL;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register URI handlers
    const httpd_uri_t uris[] = {
        { .uri = "/",           .method = HTTP_GET, .handler = handler_dashboard },
        { .uri = "/api/status", .method = HTTP_GET, .handler = handler_status },
        { .uri = "/api/set",    .method = HTTP_GET, .handler = handler_set_rpm },
        { .uri = "/api/dir",    .method = HTTP_GET, .handler = handler_set_dir },
        { .uri = "/api/start",  .method = HTTP_GET, .handler = handler_start },
        { .uri = "/api/stop",   .method = HTTP_GET, .handler = handler_stop },
        { .uri = "/api/brake",  .method = HTTP_GET, .handler = handler_brake },
        { .uri = "/api/coast",  .method = HTTP_GET, .handler = handler_coast },
    };

    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    ESP_LOGI(TAG, "HTTP server started on port %d (%d endpoints)",
             config.server_port, (int)(sizeof(uris) / sizeof(uris[0])));
    return ESP_OK;
}
