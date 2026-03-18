#include "tacho.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"

static const char *TAG = "tacho";

// ---------------------------------------------------------------------------
// We support up to 8 tacho instances. The ISR needs to know which tacho_t
// to update, so we keep a lookup table indexed by GPIO number.
// (ESP32 GPIOs go up to 39, so a 40-entry table covers everything.)
// ---------------------------------------------------------------------------
static tacho_t *s_gpio_to_tacho[40] = {0};
static bool s_isr_service_installed = false;

// ---------------------------------------------------------------------------
// GPIO ISR handler — runs in IRAM, must be fast
// Records timestamp and computes period between consecutive edges.
// ---------------------------------------------------------------------------
static void IRAM_ATTR tacho_isr_handler(void *arg)
{
    tacho_t *t = (tacho_t *)arg;
    int64_t now = esp_timer_get_time();
    int64_t prev = t->last_edge_us;

    if (prev > 0) {
        t->edge_period_us = now - prev;
    }

    t->last_edge_us = now;
    t->edge_count++;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
esp_err_t tacho_init(tacho_t *tacho, int gpio, int pcnt_unit)
{
    if (tacho->initialized) {
        ESP_LOGW(TAG, "TACHO already initialized (GPIO%d)", gpio);
        return ESP_OK;
    }

    tacho->gpio = gpio;
    tacho->pcnt_unit = pcnt_unit;
    tacho->last_edge_us = 0;
    tacho->edge_period_us = 0;
    tacho->edge_count = 0;
    tacho->last_sample_us = esp_timer_get_time();
    tacho->rpm = 0.0f;
    tacho->rpm_raw = 0.0f;

    // Install GPIO ISR service (once, shared across all tachos)
    if (!s_isr_service_installed) {
        ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
        s_isr_service_installed = true;
    }

    // Configure GPIO as input (IO34/35 are input-only, no internal pull-up)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,   // external 10k pull-up on PCB
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,      // trigger on both edges
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Register ISR for this pin
    s_gpio_to_tacho[gpio] = tacho;
    ESP_ERROR_CHECK(gpio_isr_handler_add(gpio, tacho_isr_handler, tacho));

    tacho->initialized = true;

    ESP_LOGI(TAG, "TACHO initialized: GPIO%d (period-based, %d edges/rev, ISR on ANYEDGE)",
             gpio, TACHO_EDGES_PER_REV);

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Update RPM — called from control loop at ~20 Hz
// ---------------------------------------------------------------------------
esp_err_t tacho_update_rpm(tacho_t *tacho)
{
    if (!tacho->initialized) return ESP_ERR_INVALID_STATE;

    int64_t now = esp_timer_get_time();
    int64_t period = tacho->edge_period_us;
    int64_t last_edge = tacho->last_edge_us;
    uint32_t edges = tacho->edge_count;

    // Reset edge counter for next interval (used for stall detection)
    tacho->edge_count = 0;

    // Check for timeout — no edges for too long means motor stopped
    int64_t since_last_edge = now - last_edge;
    if (last_edge == 0 || since_last_edge > TACHO_TIMEOUT_US || edges == 0) {
        tacho->rpm_raw = 0.0f;
        // Decay quickly toward zero
        tacho->rpm *= 0.5f;
        if (tacho->rpm < 0.5f) tacho->rpm = 0.0f;
        return ESP_OK;
    }

    // RPM from period between edges:
    //   One edge = 1/TACHO_EDGES_PER_REV of a revolution
    //   RPM = (1 / EDGES_PER_REV) / (period_us / 60e6)
    //       = 60e6 / (period_us * EDGES_PER_REV)
    float rpm_instant = 0.0f;
    if (period > 0) {
        rpm_instant = 60.0e6f / ((float)period * (float)TACHO_EDGES_PER_REV);
    }

    tacho->rpm_raw = rpm_instant;

    // EMA filter
    tacho->rpm = TACHO_EMA_ALPHA * rpm_instant
               + (1.0f - TACHO_EMA_ALPHA) * tacho->rpm;

    tacho->last_sample_us = now;
    return ESP_OK;
}

float tacho_get_rpm(const tacho_t *tacho)
{
    return tacho->rpm;
}
