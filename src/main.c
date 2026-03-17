#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "pin_config.h"
#include "drive_controller.h"

static const char *TAG = "main";

// Global drive controller instance
static drive_controller_t g_drive = {0};

// ---------------------------------------------------------------------------
// Status LED
// ---------------------------------------------------------------------------
static void status_led_init(void)
{
    gpio_config_t conf = {
        .pin_bit_mask = (1ULL << LED_GPIO_STATUS),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&conf);
    gpio_set_level(LED_GPIO_STATUS, 0);
}

// ---------------------------------------------------------------------------
// Main application
// ---------------------------------------------------------------------------
void app_main(void)
{
    ESP_LOGI(TAG, "=== BLDC Axis Drive System v0.1.0 ===");
    ESP_LOGI(TAG, "4x AMT49413 + ESP32 | PI closed-loop RPM control");

    // Init status LED
    status_led_init();
    gpio_set_level(LED_GPIO_STATUS, 1);

    // Initialize drive system
    esp_err_t ret = drive_controller_init(&g_drive);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Drive controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Start the control loop
    ret = drive_controller_start(&g_drive);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Drive controller start failed: %s", esp_err_to_name(ret));
        return;
    }

    // --- Demo: ramp up to target RPM ---
    // In production, replace this with CAN/UART/WiFi command input
    float target_rpm = 150.0f;  // Adjust for your motor's range
    ESP_LOGI(TAG, "Setting target RPM: %.0f", target_rpm);
    drive_controller_set_direction(&g_drive, MOTOR_DIR_FORWARD);
    drive_controller_set_rpm(&g_drive, target_rpm);

    // Main loop: periodic status reporting
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "RPM: FL=%.0f  FR=%.0f  RL=%.0f  RR=%.0f",
                 drive_controller_get_rpm(&g_drive, 0),
                 drive_controller_get_rpm(&g_drive, 1),
                 drive_controller_get_rpm(&g_drive, 2),
                 drive_controller_get_rpm(&g_drive, 3));

        if (drive_controller_is_stalled(&g_drive)) {
            ESP_LOGW(TAG, "*** STALL CONDITION DETECTED ***");
            // In production: decide whether to retry, reduce load, or shut down
        }

        // Toggle LED as heartbeat
        static int led_state = 0;
        led_state = !led_state;
        gpio_set_level(LED_GPIO_STATUS, led_state);
    }
}
