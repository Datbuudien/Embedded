#pragma once

// ============================================================
//  Config — Nút Biên (ESP32 DevKit V1 CH340)
// ============================================================

// ---- Node Identity -----------------------------------------
#define NODE_ID         0       // ID của node này (0–19)

// ---- Pinout ------------------------------------------------
// !!! CHỈ dùng ADC1 — ADC2 xung đột Wi-Fi/ESP-NOW radio !!!
#define SOIL_ADC_PIN    34      // FC28 analog → ADC1_CH6
#define RAIN_AO_PIN     35      // Cảm biến mưa AO → ADC1_CH7
#define RAIN_DO_PIN     33      // Cảm biến mưa DO (digital, active-LOW)
#define DHT_PIN         4       // DHT11 data pin
#define RELAY_PIN       5       // Relay 5V 1CH (active-LOW: LOW = bơm ON)

// ---- FC28 Soil Moisture Calibration (2-point) ---------------
// Đo thực tế: cắm sensor vào đất khô hoàn toàn → ghi ADC_DRY
//             cắm sensor vào đất bão hòa nước → ghi ADC_WET
// ADC 12-bit: 0–4095; FC28 ẩm hơn → điện trở nhỏ → ADC thấp hơn
#define SOIL_ADC_DRY    3200    // ADC khi đất khô hoàn toàn (mặc định)
#define SOIL_ADC_WET    900     // ADC khi đất bão hòa nước (mặc định)

// ---- Timing (milliseconds) ---------------------------------
#define SOIL_RAIN_INTERVAL_MS   500     // FC28 + Rain AO: ≥ 2Hz (≤ 500ms)
#define DHT_INTERVAL_MS         1000    // DHT11 max 1Hz — không poll nhanh hơn!
#define ESPNOW_SEND_INTERVAL_MS 1000    // Gửi SensorFrame mỗi 1 giây

// ---- Watchdog ----------------------------------------------
#define WDT_TIMEOUT_SEC 5       // Reset nếu loop bị block > 5 giây

// ---- Serial ------------------------------------------------
#define SERIAL_BAUD     115200

// ---- MAC Address của Nút Trung tâm (Master) ----------------
// !!! CẬP NHẬT MAC THỰC TẾ CỦA ESP32-S3 trước khi flash !!!
// Lấy MAC bằng: Serial.println(WiFi.macAddress()); ở Master
#define MASTER_MAC      {0x3C, 0x0F, 0x02, 0xDE, 0xDD, 0x64}  // TODO: thay MAC thực
