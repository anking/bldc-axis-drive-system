#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// TACHO - RPM Measurement via PCNT (Pulse Counter)
// AMT49413 TACHO output: open-drain, pulled up to 3.3V
// ============================================================================

// Number of electrical cycles per mechanical revolution
// Hoverboard motors typically have 15 pole pairs → 15 TACHO pulses/rev
// Adjust this if your motor differs
#define TACHO_PULSES_PER_REV   15

typedef struct {
    int gpio;
    int pcnt_unit;          // PCNT unit index (0–7 on ESP32)
    int32_t pulse_count;    // Last captured count
    int64_t last_sample_us; // Timestamp of last RPM computation
    float rpm;              // Computed RPM
    bool initialized;
} tacho_t;

/**
 * Initialize a TACHO input using the PCNT peripheral.
 * Counts rising edges on the specified GPIO.
 */
esp_err_t tacho_init(tacho_t *tacho, int gpio, int pcnt_unit);

/**
 * Sample the pulse counter and compute RPM.
 * Call this at a fixed interval (e.g., every 50 ms from the control loop).
 * Clears the counter after reading.
 */
esp_err_t tacho_update_rpm(tacho_t *tacho);

/**
 * Get the last computed RPM value.
 */
float tacho_get_rpm(const tacho_t *tacho);
