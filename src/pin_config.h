#pragma once

// ============================================================================
// BLDC Axis Drive System - Pin Configuration
// ESP32-WROOM-32E-N4 → 2× AMT49413 Motor Drivers (per axis board)
// Matches schematic U5
// ============================================================================

// --- Motor 1 ---
#define M1_GPIO_PWM         16      // LEDC PWM output → IO16
#define M1_GPIO_DIR         13      // Direction control → IO13
#define M1_GPIO_BRAKE       32      // nBRAKE (active low) → IO32
#define M1_GPIO_COAST       33      // COAST (active low) → IO33
#define M1_GPIO_TACHO       34      // TACHO input (input-only, 10k pullup to 3.3V) → IO34
#define M1_GPIO_FF1         23      // Fault flag 1 (input) → IO23
#define M1_GPIO_FF2         22      // Fault flag 2 (input) → IO22

// --- Motor 2 ---
#define M2_GPIO_PWM         26      // LEDC PWM output → IO26
#define M2_GPIO_DIR         25      // Direction control → IO25
#define M2_GPIO_BRAKE       27      // nBRAKE (active low) → IO27
#define M2_GPIO_COAST       14      // COAST (active low) → IO14
#define M2_GPIO_TACHO       35      // TACHO input (input-only, 10k pullup to 3.3V) → IO35
#define M2_GPIO_FF1         19      // Fault flag 1 (input) → IO19
#define M2_GPIO_FF2         18      // Fault flag 2 (input) → IO18

// --- CAN Bus (SN65HVD230, 3.3V) ---
#define CAN_GPIO_TX         4       // IO4 → TWAI TX
#define CAN_GPIO_RX         17      // IO17 → TWAI RX

// --- Status LEDs ---
#define LED_GPIO_1          21      // NET_LED1 → IO21
#define LED_GPIO_2          15      // NET_LED2 → IO15

// --- Motors on this board ---
#define MOTOR_COUNT         1       // Only M1 active (M2 driver not populated)
