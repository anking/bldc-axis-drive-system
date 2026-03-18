#include "motor_driver.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "motor_driver";

// PWM configuration
#define MOTOR_PWM_FREQ_HZ       20000   // 20 kHz — above audible range
#define MOTOR_PWM_RESOLUTION    LEDC_TIMER_10_BIT  // 0–1023
#define MOTOR_PWM_MAX_DUTY      1023

esp_err_t motor_driver_init(motor_driver_t *motor, const motor_driver_config_t *config)
{
    if (motor->initialized) {
        ESP_LOGW(TAG, "Motor already initialized (PWM GPIO %d)", config->gpio_pwm);
        return ESP_OK;
    }

    motor->config = *config;
    motor->duty_pct = 0;
    motor->direction = MOTOR_DIR_FORWARD;
    motor->braking = false;
    motor->coasting = true;

    // Configure LEDC timer
    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = config->ledc_timer,
        .duty_resolution = MOTOR_PWM_RESOLUTION,
        .freq_hz         = MOTOR_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // Configure LEDC channel
    ledc_channel_config_t chan_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = config->ledc_channel,
        .timer_sel  = config->ledc_timer,
        .gpio_num   = config->gpio_pwm,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan_conf));

    // Configure direction GPIO
    gpio_config_t dir_conf = {
        .pin_bit_mask = (1ULL << config->gpio_dir),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&dir_conf));
    gpio_set_level(config->gpio_dir, MOTOR_DIR_FORWARD);

    // Configure nBRAKE GPIO (active low: LOW = brake engaged, HIGH = normal)
    gpio_config_t brake_conf = {
        .pin_bit_mask = (1ULL << config->gpio_brake),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&brake_conf));
    gpio_set_level(config->gpio_brake, 1);  // Brake released

    // Configure COAST GPIO (active low: LOW = outputs disabled, HIGH = normal)
    gpio_config_t coast_conf = {
        .pin_bit_mask = (1ULL << config->gpio_coast),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&coast_conf));
    gpio_set_level(config->gpio_coast, 0);  // Start in coast (safe default)

    // Configure FF1 and FF2 as inputs with pull-ups (open-drain outputs from AMT49413)
    if (config->gpio_ff1 >= 0) {
        gpio_config_t ff_conf = {
            .pin_bit_mask = (1ULL << config->gpio_ff1) | (1ULL << config->gpio_ff2),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&ff_conf));
    }
    motor->ff1 = 1;
    motor->ff2 = 1;

    motor->initialized = true;
    ESP_LOGI(TAG, "Motor init: PWM=IO%d DIR=IO%d nBRAKE=IO%d COAST=IO%d FF1=IO%d FF2=IO%d (LEDC ch%d)",
             config->gpio_pwm, config->gpio_dir, config->gpio_brake,
             config->gpio_coast, config->gpio_ff1, config->gpio_ff2,
             config->ledc_channel);

    return ESP_OK;
}

esp_err_t motor_driver_set_duty(motor_driver_t *motor, int duty_pct)
{
    if (!motor->initialized) return ESP_ERR_INVALID_STATE;

    if (duty_pct < 0) duty_pct = 0;
    if (duty_pct > 100) duty_pct = 100;

    motor->duty_pct = duty_pct;

    uint32_t duty = (duty_pct * MOTOR_PWM_MAX_DUTY) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, motor->config.ledc_channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, motor->config.ledc_channel);

    return ESP_OK;
}

esp_err_t motor_driver_set_direction(motor_driver_t *motor, motor_dir_t dir)
{
    if (!motor->initialized) return ESP_ERR_INVALID_STATE;

    motor->direction = dir;
    gpio_set_level(motor->config.gpio_dir, dir);

    return ESP_OK;
}

esp_err_t motor_driver_brake(motor_driver_t *motor)
{
    if (!motor->initialized) return ESP_ERR_INVALID_STATE;

    motor->braking = true;
    motor->coasting = false;
    gpio_set_level(motor->config.gpio_coast, 1);  // Outputs enabled
    gpio_set_level(motor->config.gpio_brake, 0);  // Brake active (low)

    return ESP_OK;
}

esp_err_t motor_driver_coast(motor_driver_t *motor)
{
    if (!motor->initialized) return ESP_ERR_INVALID_STATE;

    motor->braking = false;
    motor->coasting = true;
    gpio_set_level(motor->config.gpio_brake, 1);  // Brake released
    gpio_set_level(motor->config.gpio_coast, 0);  // Outputs disabled — free-wheel

    return ESP_OK;
}

esp_err_t motor_driver_run(motor_driver_t *motor)
{
    if (!motor->initialized) return ESP_ERR_INVALID_STATE;

    motor->braking = false;
    motor->coasting = false;
    gpio_set_level(motor->config.gpio_brake, 1);  // Brake released
    gpio_set_level(motor->config.gpio_coast, 1);  // Outputs enabled

    return ESP_OK;
}

esp_err_t motor_driver_stop(motor_driver_t *motor)
{
    if (!motor->initialized) return ESP_ERR_INVALID_STATE;

    motor_driver_set_duty(motor, 0);
    motor_driver_brake(motor);

    ESP_LOGI(TAG, "Motor stopped (IO%d)", motor->config.gpio_pwm);
    return ESP_OK;
}

esp_err_t motor_driver_reset_fault(motor_driver_t *motor)
{
    if (!motor->initialized) return ESP_ERR_INVALID_STATE;

    // Zero PWM first for safety
    motor_driver_set_duty(motor, 0);

    // Cycle COAST: disable outputs → wait → re-enable
    // This clears the AMT49413 fault latch
    gpio_set_level(motor->config.gpio_coast, 0);  // COAST LOW = outputs off
    gpio_set_level(motor->config.gpio_brake, 1);  // Brake released
    vTaskDelay(pdMS_TO_TICKS(20));                 // Hold 20ms

    gpio_set_level(motor->config.gpio_coast, 1);  // COAST HIGH = outputs re-enabled

    motor->braking = false;
    motor->coasting = false;

    ESP_LOGI(TAG, "Fault reset (COAST cycle) on IO%d", motor->config.gpio_pwm);
    return ESP_OK;
}

const char *motor_driver_read_faults(motor_driver_t *motor)
{
    if (!motor->initialized || motor->config.gpio_ff1 < 0) {
        return "N/A";
    }

    motor->ff1 = gpio_get_level(motor->config.gpio_ff1);
    motor->ff2 = gpio_get_level(motor->config.gpio_ff2);

    if (motor->ff1 && motor->ff2)   return "OK";
    if (!motor->ff1 && motor->ff2)  return "SHORT/OC";
    if (motor->ff1 && !motor->ff2)  return "OPEN/UV";
    return "OVERTEMP";
}
