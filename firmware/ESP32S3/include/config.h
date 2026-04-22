#pragma once

// ============================================================
//  Config — Nút Trung tâm (Master / AI Engine)
//  Board: ESP32-S3 N16R8 (Xiaozhi S3)
// ============================================================

// ---- ESP-NOW Peer: MAC của Nút Biên (Edge) -----------------
// !!! CẬP NHẬT MAC THỰC TẾ CỦA ESP32 CH340 trước khi flash !!!
// Lấy MAC: Serial.println(WiFi.macAddress()); ở Nút Biên
#define EDGE_MAC_0  {0x68, 0xFE, 0x71, 0x0D, 0x1D, 0x94} // TODO: thay MAC thực

// ---- Pinout ------------------------------------------------
// DS3231 RTC — I²C
#define RTC_SDA_PIN     16
#define RTC_SCL_PIN     15
// Pull-up 4.7kΩ đã gắn ngoài (không dùng internal pullup)

// ---- ANN Weights & Bias ------------------------------------
// Binary classifier: P = sigmoid(w1*x1 + w2*x2 + ... + w5*x5 + b)
// x1 = soil_pct (0-100), x2 = temperature (°C), x3 = humidity_air (%),
// x4 = current_hour (0-23), x5 = rain_norm (0.0-1.0)
//
// Trọng số mặc định (trained offline, chỉnh theo thực tế vườn):
// Logic: đất khô + nhiệt cao + giờ phù hợp + không mưa → tưới
#define ANN_W1   -0.05f   // soil_pct: càng cao (ẩm) → giảm xác suất tưới
#define ANN_W2    0.02f   // temperature: cao hơn → cần tưới hơn (trước khi đạt 38°C)
#define ANN_W3   -0.01f   // humidity_air: KK ẩm → ít bốc hơi → ít cần tưới
#define ANN_W4    0.00f   // current_hour: không bias theo giờ (rule khung giờ cấm TODO)
#define ANN_W5   -3.00f   // rain_norm: mưa → không tưới (weight âm mạnh)
#define ANN_BIAS  2.50f   // bias: threshold baseline
#define ANN_THRESHOLD 0.5f  // P > 0.5 → CMD_ON

// Hard exception thresholds (không override được)
#define TEMP_MAX_WATER  38.0f   // Sốc nhiệt rễ cây (Thermal Shock) — KI-002

// ---- Manual Override ---------------------------------------
#define MANUAL_OVERRIDE_TIMEOUT_SEC  1800   // 30 phút — KI-008, không disable được

// ---- SoftAP Wi-Fi ------------------------------------------
#define SOFTAP_SSID     "TuoiTieu-AP"
#define SOFTAP_PASS     "12345678"
#define WEB_SERVER_PORT 80

// ---- Timing (milliseconds) ---------------------------------
#define INFERENCE_INTERVAL_MS    1000   // Chạy ANN mỗi 1 giây
#define DISPLAY_REFRESH_MS       1000   // Cập nhật LVGL display mỗi 1 giây
#define STATUS_LOG_INTERVAL_MS   5000   // In log serial mỗi 5 giây

// ---- Watchdog ----------------------------------------------
#define WDT_TIMEOUT_SEC  5

// ---- Serial ------------------------------------------------
#define SERIAL_BAUD  115200
