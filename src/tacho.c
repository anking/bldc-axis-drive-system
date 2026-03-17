#include "tacho.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "tacho";

// Internal storage for PCNT unit handles (one per tacho instance)
static pcnt_unit_handle_t s_pcnt_units[8] = {0};

esp_err_t tacho_init(tacho_t *tacho, int gpio, int pcnt_unit)
{
    if (tacho->initialized) {
        ESP_LOGW(TAG, "TACHO already initialized (GPIO%d)", gpio);
        return ESP_OK;
    }

    tacho->gpio = gpio;
    tacho->pcnt_unit = pcnt_unit;
    tacho->pulse_count = 0;
    tacho->last_sample_us = 0;
    tacho->rpm = 0.0f;

    // Configure PCNT unit
    pcnt_unit_config_t unit_config = {
        .high_limit = 32767,
        .low_limit  = -1,      // We only count up
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &s_pcnt_units[pcnt_unit]));

    // Configure PCNT channel: count rising edges on the GPIO
    pcnt_chan_config_t chan_config = {
        .edge_gpio_num  = gpio,
        .level_gpio_num = -1,   // No level signal
    };
    pcnt_channel_handle_t chan = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt_units[pcnt_unit], &chan_config, &chan));

    // Count +1 on rising edge, ignore falling edge
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // Rising
        PCNT_CHANNEL_EDGE_ACTION_HOLD));     // Falling

    // No level-based action
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP));

    // Enable glitch filter (1 µs — rejects noise spikes)
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt_units[pcnt_unit], &filter_config));

    // Enable and start counting
    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt_units[pcnt_unit]));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_units[pcnt_unit]));
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt_units[pcnt_unit]));

    tacho->last_sample_us = esp_timer_get_time();
    tacho->initialized = true;

    ESP_LOGI(TAG, "TACHO initialized: GPIO%d (PCNT unit %d, %d pulses/rev)",
             gpio, pcnt_unit, TACHO_PULSES_PER_REV);

    return ESP_OK;
}

esp_err_t tacho_update_rpm(tacho_t *tacho)
{
    if (!tacho->initialized) return ESP_ERR_INVALID_STATE;

    int count = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(s_pcnt_units[tacho->pcnt_unit], &count));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_units[tacho->pcnt_unit]));

    int64_t now_us = esp_timer_get_time();
    int64_t dt_us = now_us - tacho->last_sample_us;
    tacho->last_sample_us = now_us;

    if (dt_us <= 0) {
        tacho->rpm = 0.0f;
        return ESP_OK;
    }

    // RPM = (pulses / pulses_per_rev) / (dt_seconds / 60)
    //     = (pulses * 60 * 1e6) / (pulses_per_rev * dt_us)
    float revolutions = (float)count / (float)TACHO_PULSES_PER_REV;
    float dt_sec = (float)dt_us / 1e6f;
    tacho->rpm = (revolutions / dt_sec) * 60.0f;

    tacho->pulse_count = count;

    return ESP_OK;
}

float tacho_get_rpm(const tacho_t *tacho)
{
    return tacho->rpm;
}
