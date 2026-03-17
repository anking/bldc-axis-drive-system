#include "drive_controller.h"
#include "pin_config.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drive_ctrl";

// Motor configurations: {pwm_gpio, dir_gpio, brake_gpio, coast_gpio, ledc_channel, ledc_timer}
static const motor_driver_config_t s_motor_configs[MOTOR_COUNT] = {
    { M1_GPIO_PWM, M1_GPIO_DIR, M1_GPIO_BRAKE, M1_GPIO_COAST, 0, 0 },  // M1 - timer 0
    { M2_GPIO_PWM, M2_GPIO_DIR, M2_GPIO_BRAKE, M2_GPIO_COAST, 1, 0 },  // M2 - timer 0
};

// TACHO GPIOs indexed by motor
static const int s_tacho_gpios[MOTOR_COUNT] = {
    M1_GPIO_TACHO, M2_GPIO_TACHO
};

static TaskHandle_t s_control_task = NULL;

// ---------------------------------------------------------------------------
// Control loop task
// ---------------------------------------------------------------------------
static void control_loop_task(void *arg)
{
    drive_controller_t *dc = (drive_controller_t *)arg;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(DRIVE_LOOP_PERIOD_MS);

    ESP_LOGI(TAG, "Control loop started (%d ms period)", DRIVE_LOOP_PERIOD_MS);

    while (dc->running) {
        for (int i = 0; i < MOTOR_COUNT; i++) {
            drive_axis_t *axis = &dc->axes[i];

            // Update RPM measurement
            tacho_update_rpm(&axis->tacho);
            float measured_rpm = tacho_get_rpm(&axis->tacho);

            // Run PI controller
            float duty = pi_controller_update(&axis->pi,
                                               axis->target_rpm,
                                               measured_rpm,
                                               DRIVE_LOOP_DT);

            // Apply duty cycle
            motor_driver_set_duty(&axis->motor, (int)duty);

            // Stall detection
            if (axis->motor.duty_pct >= DRIVE_STALL_DUTY_PCT &&
                measured_rpm < DRIVE_STALL_RPM &&
                axis->target_rpm > 0.0f) {
                axis->stall_count++;
                if (axis->stall_count >= DRIVE_STALL_COUNT_MAX && !axis->stalled) {
                    axis->stalled = true;
                    ESP_LOGW(TAG, "STALL detected on motor %d (duty=%d%%, rpm=%.1f)",
                             i, axis->motor.duty_pct, measured_rpm);
                }
            } else {
                axis->stall_count = 0;
                axis->stalled = false;
            }
        }

        vTaskDelayUntil(&last_wake, period);
    }

    // Stop all motors when task exits
    for (int i = 0; i < MOTOR_COUNT; i++) {
        motor_driver_stop(&dc->axes[i].motor);
    }

    ESP_LOGI(TAG, "Control loop stopped");
    s_control_task = NULL;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t drive_controller_init(drive_controller_t *dc)
{
    if (dc->initialized) {
        ESP_LOGW(TAG, "Drive controller already initialized");
        return ESP_OK;
    }

    memset(dc, 0, sizeof(*dc));

    for (int i = 0; i < MOTOR_COUNT; i++) {
        drive_axis_t *axis = &dc->axes[i];

        // Init motor driver
        esp_err_t ret = motor_driver_init(&axis->motor, &s_motor_configs[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init motor %d: %s", i, esp_err_to_name(ret));
            return ret;
        }

        // Init TACHO (PCNT unit = motor index)
        ret = tacho_init(&axis->tacho, s_tacho_gpios[i], i);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init tacho %d: %s", i, esp_err_to_name(ret));
            return ret;
        }

        // Init PI controller
        pi_controller_init(&axis->pi,
                           DRIVE_DEFAULT_KP, DRIVE_DEFAULT_KI,
                           0.0f, 100.0f);

        axis->target_rpm = 0.0f;
        axis->stall_count = 0;
        axis->stalled = false;
    }

    dc->initialized = true;
    dc->running = false;

    ESP_LOGI(TAG, "Drive controller initialized (%d motors)", MOTOR_COUNT);
    return ESP_OK;
}

esp_err_t drive_controller_start(drive_controller_t *dc)
{
    if (!dc->initialized) return ESP_ERR_INVALID_STATE;
    if (dc->running) return ESP_OK;

    dc->running = true;

    // Enable outputs on all motors (exit coast, release brake)
    for (int i = 0; i < MOTOR_COUNT; i++) {
        motor_driver_run(&dc->axes[i].motor);
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        control_loop_task,
        "drive_ctrl",
        4096,
        dc,
        5,                  // Priority 5 — above normal
        &s_control_task,
        1                   // Pin to core 1 (core 0 handles WiFi/system)
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create control task");
        dc->running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t drive_controller_stop(drive_controller_t *dc)
{
    if (!dc->running) return ESP_OK;

    dc->running = false;

    // Wait for task to finish
    while (s_control_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Ensure all motors are stopped
    for (int i = 0; i < MOTOR_COUNT; i++) {
        motor_driver_stop(&dc->axes[i].motor);
        pi_controller_reset(&dc->axes[i].pi);
    }

    ESP_LOGI(TAG, "Drive controller stopped");
    return ESP_OK;
}

void drive_controller_set_rpm(drive_controller_t *dc, float rpm)
{
    for (int i = 0; i < MOTOR_COUNT; i++) {
        dc->axes[i].target_rpm = rpm;
    }
}

void drive_controller_set_differential(drive_controller_t *dc, float left_rpm, float right_rpm)
{
    dc->axes[0].target_rpm = left_rpm;   // M1
    dc->axes[1].target_rpm = right_rpm;  // M2
}

void drive_controller_set_direction(drive_controller_t *dc, motor_dir_t dir)
{
    for (int i = 0; i < MOTOR_COUNT; i++) {
        motor_driver_set_direction(&dc->axes[i].motor, dir);
    }
}

void drive_controller_emergency_stop(drive_controller_t *dc)
{
    // Immediately zero all motors — don't wait for control loop
    for (int i = 0; i < MOTOR_COUNT; i++) {
        motor_driver_stop(&dc->axes[i].motor);
        dc->axes[i].target_rpm = 0.0f;
        pi_controller_reset(&dc->axes[i].pi);
    }
    ESP_LOGW(TAG, "EMERGENCY STOP");
}

float drive_controller_get_rpm(const drive_controller_t *dc, int axis)
{
    if (axis < 0 || axis >= MOTOR_COUNT) return 0.0f;
    return tacho_get_rpm(&dc->axes[axis].tacho);
}

bool drive_controller_is_stalled(const drive_controller_t *dc)
{
    for (int i = 0; i < MOTOR_COUNT; i++) {
        if (dc->axes[i].stalled) return true;
    }
    return false;
}
