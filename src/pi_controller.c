#include "pi_controller.h"

void pi_controller_init(pi_controller_t *pi, float kp, float ki,
                         float output_min, float output_max)
{
    pi->kp = kp;
    pi->ki = ki;
    pi->integral = 0.0f;
    pi->output_min = output_min;
    pi->output_max = output_max;
    // Anti-windup limit: prevent integral from exceeding output range
    pi->integral_max = (output_max - output_min) * 2.0f;
}

float pi_controller_update(pi_controller_t *pi, float setpoint, float measured, float dt)
{
    float error = setpoint - measured;

    // Accumulate integral
    pi->integral += error * dt;

    // Anti-windup clamp
    if (pi->integral > pi->integral_max) pi->integral = pi->integral_max;
    if (pi->integral < -pi->integral_max) pi->integral = -pi->integral_max;

    // Compute output
    float output = (pi->kp * error) + (pi->ki * pi->integral);

    // Clamp output
    if (output > pi->output_max) output = pi->output_max;
    if (output < pi->output_min) output = pi->output_min;

    return output;
}

void pi_controller_reset(pi_controller_t *pi)
{
    pi->integral = 0.0f;
}
