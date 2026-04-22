#pragma once
#include <stdint.h>
#include <stdbool.h>

// ============================================================
//  ANN Inference — Edge AI, bare-metal C++
//  Binary classifier: tưới (1) hoặc không tưới (0)
//  Chạy trên FPU hardware của LX7, < 1ms/cycle
// ============================================================

/**
 * Khởi tạo ANN (nạp weights từ config.h).
 * Gọi một lần trong setup().
 */
void ann_init();

/**
 * Chạy inference, trả về CMD_ON (0x01) hoặc CMD_OFF (0x00).
 *
 * Hard exceptions TRƯỚC sigmoid (không override được — KI-002, KI-009):
 *   - temp > 38.0°C → CMD_OFF (Thermal Shock)
 *   - rain_digital == true → CMD_OFF (đang mưa)
 *
 * @param soil_pct    độ ẩm đất 0.0–100.0 %
 * @param temp        nhiệt độ không khí °C
 * @param hum         độ ẩm không khí %RH
 * @param hour        giờ hiện tại 0–23 (từ DS3231)
 * @param rain_norm   mức mưa analog 0.0–1.0 (rain_raw / 4095.0)
 * @param rain_digital true nếu cảm biến DO báo đang mưa
 * @return CMD_ON hoặc CMD_OFF
 */
uint8_t ann_infer(float soil_pct, float temp, float hum,
                  uint8_t hour, float rain_norm, bool rain_digital);

/**
 * Trả về xác suất P (0.0–1.0) từ lần inference gần nhất.
 * Dùng để hiển thị trên Web UI / LCD.
 */
float ann_get_last_prob();
