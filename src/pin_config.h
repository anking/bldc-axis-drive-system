#pragma once

// ============================================================================
// BLDC Axis Drive System - Pin Configuration
// ESP32-WROOM-32E → 4× AMT49413 Motor Drivers
// ============================================================================

// --- Motor 1 (Front Left) ---
#define M1_GPIO_PWM         25      // LEDC PWM output
#define M1_GPIO_DIR         26      // Direction control
#define M1_GPIO_BRAKE       27      // Brake (active low on AMT49413)
#define M1_GPIO_TACHO       13      // TACHO pulse input (open-drain, 10k pullup to 3.3V)

// --- Motor 2 (Front Right) ---
#define M2_GPIO_PWM         32      // LEDC PWM output
#define M2_GPIO_DIR         33      // Direction control
#define M2_GPIO_BRAKE       14      // Brake
#define M2_GPIO_TACHO       16      // TACHO pulse input

// --- Motor 3 (Rear Left) ---
#define M3_GPIO_PWM         17      // LEDC PWM output
#define M3_GPIO_DIR         5       // Direction control
#define M3_GPIO_BRAKE       18      // Brake
#define M3_GPIO_TACHO       34      // TACHO pulse input (input-only GPIO)

// --- Motor 4 (Rear Right) ---
#define M4_GPIO_PWM         19      // LEDC PWM output
#define M4_GPIO_DIR         21      // Direction control
#define M4_GPIO_BRAKE       22      // Brake
#define M4_GPIO_TACHO       35      // TACHO pulse input (input-only GPIO)

// --- CAN Bus (SN65HVD230) ---
#define CAN_GPIO_TX         23
#define CAN_GPIO_RX         4

// --- Status LED (optional) ---
#define LED_GPIO_STATUS     2       // Onboard LED on most ESP32 devkits

// --- Motor count ---
#define MOTOR_COUNT         4
