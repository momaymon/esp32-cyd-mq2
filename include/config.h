#pragma once

// ---------- Wiring ----------
// MQ-2 AO -> GPIO35 (P3 connector, ADC1, input only)
// MQ-2 DO -> GPIO22 (optional, set to -1 if not wired)
#define MQ2_AO_PIN 35
#define MQ2_DO_PIN 22

// MQ-2 module runs on 5V, so AO can reach 5V. ESP32 ADC max is 3.3V.
// Put a divider on AO: AO --10k--+--20k-- GND, tap (+) to GPIO35.
// DIVIDER_RATIO = (R1 + R2) / R2. No divider -> 1.0 (only if module is on 3.3V).
#define DIVIDER_RATIO 1.5f

// Module supply voltage and load resistor (most MQ-2 boards: "102" = 1k, some "103" = 10k)
#define MQ2_VCC 5.0f
#define MQ2_RL_KOHM 1.0f

// Rs/R0 in clean air from the MQ-2 datasheet
#define CLEAN_AIR_RATIO 9.83f

// ---------- Timing ----------
#define WARMUP_SECONDS 60      // heater warm-up before trusting readings
#define CALIB_SAMPLES 50       // samples to measure R0 after warm-up
#define READ_INTERVAL_MS 500

// ---------- Alarm thresholds (LPG ppm estimate) ----------
#define PPM_WARN 300
#define PPM_DANGER 1000

// ---------- On-board RGB LED (active LOW) ----------
#define LED_R 4
#define LED_G 16
#define LED_B 17
