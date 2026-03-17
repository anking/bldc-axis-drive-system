#pragma once

#include "esp_err.h"
#include "motor_driver.h"
#include "tacho.h"
#include "pi_controller.h"

// ============================================================================
// Drive Controller - 4-Motor Coordination with PI Speed Control
// ============================================================================

// PI tuning defaults — adjust for your motor/load
#define DRIVE_DEFAULT_KP        0.15f   // Start conservative
#define DRIVE_DEFAULT_KI        0.4f    // Integral catches steady-state error

// Control loop timing
#define DRIVE_LOOP_PERIOD_MS    50      // 20 Hz control loop
#define DRIVE_LOOP_DT           (DRIVE_LOOP_PERIOD_MS / 1000.0f)

// Stall detection
#define DRIVE_STALL_RPM         5.0f    // Below this RPM with duty > threshold = stall
#define DRIVE_STALL_DUTY_PCT    20      // Minimum duty to consider stall condition
#define DRIVE_STALL_COUNT_MAX   20      // Consecutive stall samples before flagging (1 sec at 20 Hz)

typedef struct {
    motor_driver_t  motor;
    tacho_t         tacho;
    pi_controller_t pi;
    float           target_rpm;
    int             stall_count;
    bool            stalled;
} drive_axis_t;

typedef struct {
    drive_axis_t axes[4];   // FL, FR, RL, RR
    bool initialized;
    bool running;
} drive_controller_t;

/**
 * Initialize all 4 motor axes (drivers, tachos, PI controllers).
 */
esp_err_t drive_controller_init(drive_controller_t *dc);

/**
 * Start the control loop FreeRTOS task.
 */
esp_err_t drive_controller_start(drive_controller_t *dc);

/**
 * Stop the control loop and all motors.
 */
esp_err_t drive_controller_stop(drive_controller_t *dc);

/**
 * Set target RPM for all 4 motors.
 */
void drive_controller_set_rpm(drive_controller_t *dc, float rpm);

/**
 * Set target RPM per side (differential steering).
 * @param left_rpm   Target RPM for left motors (M1, M3)
 * @param right_rpm  Target RPM for right motors (M2, M4)
 */
void drive_controller_set_differential(drive_controller_t *dc, float left_rpm, float right_rpm);

/**
 * Set direction for all motors.
 */
void drive_controller_set_direction(drive_controller_t *dc, motor_dir_t dir);

/**
 * Emergency stop all motors immediately.
 */
void drive_controller_emergency_stop(drive_controller_t *dc);

/**
 * Get current RPM for a specific axis (0–3).
 */
float drive_controller_get_rpm(const drive_controller_t *dc, int axis);

/**
 * Check if any axis is stalled.
 */
bool drive_controller_is_stalled(const drive_controller_t *dc);
