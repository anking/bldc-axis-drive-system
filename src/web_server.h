#pragma once

#include "esp_err.h"
#include "drive_controller.h"

// ============================================================================
// HTTP Web Server - Dashboard + REST API
// Serves on port 80 (default)
//   GET /            → HTML dashboard
//   GET /api/status  → JSON system + motor status
//   GET /api/set     → ?rpm=<float>&motor=<0|1|all>
//   GET /api/dir     → ?dir=<fwd|rev>&motor=<0|1|all>
//   GET /api/start   → start control loop
//   GET /api/stop    → emergency stop
//   GET /api/brake   → engage electrical brake
//   GET /api/coast   → freewheel mode
// ============================================================================

/**
 * Start HTTP server and register all URI handlers.
 * @param dc  Pointer to the drive controller (must outlive the server).
 */
esp_err_t web_server_init(drive_controller_t *dc);
