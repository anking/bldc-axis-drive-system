#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// TACHO - RPM Measurement via Edge Period Timing (GPIO ISR)
// AMT49413 TACHO output: open-drain, pulled up to 3.3V
//
// Measures time between consecutive edges for sub-RPM resolution.
// At 80 RPM with 30 edges/rev: edge every ~25ms, 1µs timer → 0.003 RPM resolution
// ============================================================================

// Number of electrical cycles per mechanical revolution
// Hoverboard motors typically have 15 pole pairs → 15 TACHO pulses/rev
// We trigger on BOTH edges (rising + falling) → 30 edges per revolution
#define TACHO_PULSES_PER_REV   15
#define TACHO_EDGES_PER_REV    (TACHO_PULSES_PER_REV * 2)

// Exponential moving average filter (0.0–1.0)
// Lower = smoother but slower response; higher = noisier but faster
#define TACHO_EMA_ALPHA        0.4f

// If no edge received for this many µs, assume motor stopped
#define TACHO_TIMEOUT_US       500000  // 500ms → below ~4 RPM = stopped

typedef struct {
    int gpio;
    int pcnt_unit;              // kept for ID, not used for counting anymore
    volatile int64_t last_edge_us;   // timestamp of most recent edge (ISR-written)
    volatile int64_t edge_period_us; // time between last two edges (ISR-written)
    volatile uint32_t edge_count;    // total edges since last update (ISR-written)
    int64_t last_sample_us;     // timestamp of last RPM computation
    float rpm;                  // Filtered RPM (EMA)
    float rpm_raw;              // Unfiltered RPM (instantaneous)
    bool initialized;
} tacho_t;

/**
 * Initialize a TACHO input using GPIO interrupt on both edges.
 */
esp_err_t tacho_init(tacho_t *tacho, int gpio, int pcnt_unit);

/**
 * Compute RPM from edge timing.
 * Call this at a fixed interval (e.g., every 50 ms from the control loop).
 */
esp_err_t tacho_update_rpm(tacho_t *tacho);

/**
 * Get the last computed RPM value.
 */
float tacho_get_rpm(const tacho_t *tacho);
