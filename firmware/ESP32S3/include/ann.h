#pragma once
#include <stdint.h>
#include <stdbool.h>

// ============================================================
//  ANN Inference — Edge AI, bare-metal C++
//  Kien truc: Input(4) -> Hidden(8, ReLU) -> Output(1, Sigmoid)
//  Trong so: weights_q8.h (INT8 quantized, ~3x nhanh hon float32)
//  Chay tren FPU hardware cua LX7, < 1ms/cycle
// ============================================================

/**
 * Khoi tao ANN — log kien truc ra Serial.
 * Goi mot lan trong setup().
 */
void ann_init();

/**
 * Chay inference, tra ve CMD_ON (0x01) hoac CMD_OFF (0x00).
 *
 * Hard exceptions TRUOC sigmoid (khong override duoc — KI-002, KI-009):
 *   - temp > 38.0 C  → CMD_OFF (Thermal Shock)
 *   - rain_digital == true → CMD_OFF (dang mua)
 *
 * @param soil_pct    do am dat 0.0-100.0 %
 * @param temp        nhiet do khong khi C
 * @param hum         do am khong khi %RH
 * @param hour        gio hien tai 0-23 (tu DS3231)
 * @param rain_digital true neu cam bien DO bao dang mua
 * @return CMD_ON hoac CMD_OFF
 */
uint8_t ann_infer(float soil_pct, float temp, float hum,
                  uint8_t hour, bool rain_digital);

/**
 * Tra ve xac suat P (0.0-1.0) tu lan inference gan nhat.
 * Dung de hien thi tren Web UI / LCD AI Confidence bar.
 */
float ann_get_last_prob();
