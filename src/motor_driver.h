#pragma once

#include "esp_err.h"
#include <stdbool.h>

// ============================================================================
// AMT49413 Motor Driver Interface
// Controls PWM speed, direction, and brake per motor
// ============================================================================

typedef enum {
    MOTOR_DIR_FORWARD = 0,
    MOTOR_DIR_REVERSE = 1
} motor_dir_t;

typedef struct {
    int gpio_pwm;
    int gpio_dir;
    int gpio_brake;
    int ledc_channel;       // LEDC channel index (0–7)
    int ledc_timer;         // LEDC timer index (0–3)
} motor_driver_config_t;

typedef struct {
    motor_driver_config_t config;
    int duty_pct;           // Current duty cycle 0–100
    motor_dir_t direction;
    bool braking;
    bool initialized;
} motor_driver_t;

/**
 * Initialize a motor driver instance.
 * Configures LEDC PWM, direction GPIO, and brake GPIO.
 */
esp_err_t motor_driver_init(motor_driver_t *motor, const motor_driver_config_t *config);

/**
 * Set PWM duty cycle (0–100 percent).
 */
esp_err_t motor_driver_set_duty(motor_driver_t *motor, int duty_pct);

/**
 * Set motor direction.
 */
esp_err_t motor_driver_set_direction(motor_driver_t *motor, motor_dir_t dir);

/**
 * Activate brake (sets BRAKE pin low on AMT49413).
 */
esp_err_t motor_driver_brake(motor_driver_t *motor);

/**
 * Release brake and coast.
 */
esp_err_t motor_driver_coast(motor_driver_t *motor);

/**
 * Emergency stop: zero PWM + brake.
 */
esp_err_t motor_driver_stop(motor_driver_t *motor);
