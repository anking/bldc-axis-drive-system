#pragma once

// ============================================================================
// PI Controller for RPM Regulation
// ============================================================================

typedef struct {
    float kp;               // Proportional gain
    float ki;               // Integral gain
    float integral;         // Accumulated integral term
    float output_min;       // Output clamp minimum (e.g., 0)
    float output_max;       // Output clamp maximum (e.g., 100)
    float integral_max;     // Anti-windup: max integral magnitude
} pi_controller_t;

/**
 * Initialize PI controller with gains and output limits.
 */
void pi_controller_init(pi_controller_t *pi, float kp, float ki,
                         float output_min, float output_max);

/**
 * Compute one PI iteration.
 * @param setpoint  Target value (RPM)
 * @param measured  Current measured value (RPM)
 * @param dt        Time step in seconds
 * @return          Controller output (duty cycle 0–100)
 */
float pi_controller_update(pi_controller_t *pi, float setpoint, float measured, float dt);

/**
 * Reset integral accumulator (e.g., on direction change or stop).
 */
void pi_controller_reset(pi_controller_t *pi);
