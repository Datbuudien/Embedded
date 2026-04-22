#pragma once
#include <stdint.h>
#include <stdbool.h>

// ============================================================
//  Shared Protocol — ESP-NOW Frames
//  Dùng chung cho Nút Biên (ESP32 CH340) và Nút Trung tâm (S3)
//  QUAN TRỌNG: __attribute__((packed)) — không padding,
//              sizeof(SensorFrame) phải < 250 bytes (giới hạn ESP-NOW)
// ============================================================

// ---- CRC-8 (polynomial 0x07) --------------------------------
/**
 * Tính CRC-8 cho buffer `len` bytes.
 * Dùng để phát hiện lỗi truyền trên kênh ESP-NOW.
 */
inline uint8_t crc8_compute(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else            crc <<= 1;
        }
    }
    return crc;
}

// ---- Sensor Frame (Nút Biên → Nút Trung tâm) ---------------
typedef struct __attribute__((packed)) {
    uint8_t  node_id;           // 0–19, định danh node
    uint32_t timestamp_ms;      // millis() từ boot của Nút Biên
    uint16_t soil_raw;          // ADC 12-bit, 0–4095 (GPIO34, ADC1_CH6)
    float    soil_pct;          // 0.0–100.0 % (sau 2-point calibration)
    float    temperature;       // °C (DHT11)
    float    humidity_air;      // %RH (DHT11)
    uint16_t rain_raw;          // ADC 12-bit, 0–4095 (GPIO35, ADC1_CH7)
    bool     rain_digital;      // GPIO33 DO: true = đang mưa (active-LOW → LOW = mưa)
    bool     pump_state;        // trạng thái relay thực tế lúc gửi
    uint8_t  crc8;              // CRC-8 của tất cả bytes trước trường này
} SensorFrame;                  // sizeof ≈ 22 bytes << 250 bytes limit ✓

// ---- Command Frame (Nút Trung tâm → Nút Biên) --------------
typedef struct __attribute__((packed)) {
    uint8_t  target_node_id;    // id của node đích
    uint8_t  command;           // 0x01 = BẬT bơm, 0x00 = TẮT bơm, 0xFF = QUERY
    uint8_t  source;            // 0 = AI_AUTO, 1 = MANUAL_WEB
    uint16_t duration_sec;      // 0 = vô hạn (đến lệnh tiếp theo)
    uint8_t  crc8;              // CRC-8
} CommandFrame;                 // sizeof = 7 bytes ✓

// ---- Command constants --------------------------------------
#define CMD_OFF     0x00
#define CMD_ON      0x01
#define CMD_QUERY   0xFF

#define SRC_AI_AUTO    0
#define SRC_MANUAL_WEB 1
