#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"

#include "pin_config.h"
#include "drive_controller.h"
#include "wifi_ap.h"
#include "web_server.h"

static const char *TAG = "main";

static drive_controller_t s_drive;

// ---------------------------------------------------------------------------
// Status LED task — blinks LEDs to indicate system state
//   Both alternating  = idle (no target RPM)
//   LED1 solid + LED2 off = running
//   Both fast flash   = fault / stall
// ---------------------------------------------------------------------------
static void status_led_task(void *arg)
{
    drive_controller_t *dc = (drive_controller_t *)arg;
    int state = 0;

    while (1) {
        bool stalled = drive_controller_is_stalled(dc);

        if (stalled) {
            // Fast alternating flash — fault
            gpio_set_level(LED_GPIO_1,  state);
            gpio_set_level(LED_GPIO_2, !state);
            state = !state;
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (dc->running) {
            // Solid LED1, LED2 off — normal operation
            gpio_set_level(LED_GPIO_1, 1);
            gpio_set_level(LED_GPIO_2, 0);
            vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            // Slow alternating — idle / ready
            gpio_set_level(LED_GPIO_1,  state);
            gpio_set_level(LED_GPIO_2, !state);
            state = !state;
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

void app_main(void)
{
    // ---------------------------------------------------------------
    // GPIO15 is a strapping pin (MTDO) — must be HIGH at boot or
    // the ROM bootloader suppresses UART0 output.
    // Force it HIGH immediately before any printf.
    // ---------------------------------------------------------------
    gpio_reset_pin(LED_GPIO_2);
    gpio_set_direction(LED_GPIO_2, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO_2, 1);

    printf("\r\n");
    printf("========================================\r\n");
    printf("  BLDC Axis Drive System v0.1.0\r\n");
    printf("========================================\r\n");

    // Chip info
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("Chip:    ESP32, %d cores, rev %d\r\n", chip.cores, chip.revision);
    printf("IDF:     %s\r\n", esp_get_idf_version());
    printf("Heap:    %lu bytes free\r\n",
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    printf("========================================\r\n\r\n");

    // Configure status LEDs
    gpio_config(&(gpio_config_t){
        .pin_bit_mask = (1ULL << LED_GPIO_1) | (1ULL << LED_GPIO_2),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    });

    // Initialize drive controller (motors, tachos, PI controllers)
    ESP_LOGI(TAG, "Initializing drive controller...");
    esp_err_t ret = drive_controller_init(&s_drive);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Drive controller init FAILED: %s", esp_err_to_name(ret));
        // Rapid flash both LEDs to signal init failure
        int s = 0;
        while (1) {
            gpio_set_level(LED_GPIO_1, s);
            gpio_set_level(LED_GPIO_2, s);
            s = !s;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    ESP_LOGI(TAG, "Drive controller ready");

    // Start WiFi AP
    ESP_LOGI(TAG, "Starting WiFi AP...");
    ret = wifi_ap_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi AP init FAILED: %s", esp_err_to_name(ret));
    }

    // Start web server
    ESP_LOGI(TAG, "Starting web server...");
    ret = web_server_init(&s_drive);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Web server init FAILED: %s", esp_err_to_name(ret));
    }

    // Start status LED task
    xTaskCreate(status_led_task, "status_led", 2048, &s_drive, 2, NULL);

    // Don't auto-start drive — boot in coast/idle, use dashboard to start
    // drive_controller_start(&s_drive);

    ESP_LOGI(TAG, "System ready — dashboard at http://192.168.4.1 (motor idle, use Start button)");

    // Main loop — periodic status logging
    while (1) {
        ESP_LOGI(TAG, "M0: %.1f RPM  heap=%lu  %s",
                 drive_controller_get_rpm(&s_drive, 0),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                 drive_controller_is_stalled(&s_drive) ? "STALL!" : "OK");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
