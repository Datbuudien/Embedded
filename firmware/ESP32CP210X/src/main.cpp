/**
 * main.cpp — Nút Biên (Edge Actuator)
 * Board: ESP32 DevKit V1 CH340
 * Vai trò: Thu thập cảm biến, đóng gói SensorFrame, gửi qua ESP-NOW,
 *          nhận CommandFrame từ Master, điều khiển Relay bơm.
 *
 * Code Rules:
 *  - Không dùng delay() trong loop — dùng millis() non-blocking timer
 *  - Chỉ dùng ADC1 (GPIO 34, 35) — ADC2 xung đột ESP-NOW radio
 *  - Relay active-LOW: LOW = bơm ON, HIGH = bơm OFF
 *  - CRC-8 bắt buộc trên mọi frame
 *  - WDT hardware 5s — feed mỗi vòng loop
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <DHTesp.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "protocol.h"

// ============================================================
//  Global State
// ============================================================

// Trạng thái cảm biến (cập nhật bởi timer)
static uint16_t g_soil_raw      = 0;
static float    g_soil_pct      = 0.0f;
static float    g_temperature   = 0.0f;
static float    g_humidity_air  = 0.0f;
static uint16_t g_rain_raw      = 0;
static bool     g_rain_digital  = false;  // true = đang mưa
static bool     g_pump_state    = false;  // trạng thái relay thực tế

// Timer millis
static uint32_t g_last_soil_rain_ms = 0;
static uint32_t g_last_dht_ms       = 0;
static uint32_t g_last_send_ms      = 0;

// DHT11
static DHTesp dht;

// MAC địa chỉ Master (Nút Trung tâm) — khớp với MASTER_MAC trong config.h
static uint8_t g_master_mac[] = MASTER_MAC;

// ============================================================
//  Hàm hỗ trợ
// ============================================================

/**
 * Chuyển đổi giá trị ADC raw của FC28 sang phần trăm độ ẩm đất.
 * 2-Point calibration: ADC_DRY=0%, ADC_WET=100%
 * FC28: càng ẩm → điện trở giảm → ADC giảm → pct tăng (nghịch đảo)
 */
static float soilRawToPct(uint16_t raw) {
    if (raw >= SOIL_ADC_DRY) return 0.0f;
    if (raw <= SOIL_ADC_WET) return 100.0f;
    // Map tuyến tính ngược chiều ADC
    float pct = (float)(SOIL_ADC_DRY - raw) / (float)(SOIL_ADC_DRY - SOIL_ADC_WET) * 100.0f;
    return pct;
}

/**
 * Đọc cảm biến FC28 (soil) và Rain sensor (AO + DO).
 * Chỉ dùng ADC1 — xem config.h.
 */
static void readSoilAndRain() {
    // FC28 — ADC1_CH6, GPIO34
    g_soil_raw = (uint16_t)analogRead(SOIL_ADC_PIN);
    g_soil_pct = soilRawToPct(g_soil_raw);

    // Rain sensor AO — ADC1_CH7, GPIO35
    // ADC càng thấp → bề mặt ướt hơn
    g_rain_raw = (uint16_t)analogRead(RAIN_AO_PIN);

    // Rain sensor DO — GPIO33, active-LOW: LOW = đang mưa
    // KI-009: logic đảo, phải dùng == LOW
    g_rain_digital = (digitalRead(RAIN_DO_PIN) == LOW);
}

/**
 * Đọc DHT11: nhiệt độ và độ ẩm không khí.
 * KI-005: max 1Hz — chỉ gọi khi đã qua DHT_INTERVAL_MS.
 */
static void readDHT() {
    TempAndHumidity data = dht.getTempAndHumidity();
    if (dht.getStatus() == DHTesp::ERROR_NONE) {
        g_temperature  = data.temperature;
        g_humidity_air = data.humidity;
    } else {
        // Giữ giá trị cũ nếu đọc lỗi, log ra serial
        Serial.printf("[DHT] Lỗi đọc: %s\n", dht.getStatusString());
    }
}

/**
 * Xây dựng SensorFrame từ giá trị cảm biến hiện tại.
 * Tính CRC-8 trên toàn bộ frame trừ byte cuối (crc8 field).
 */
static SensorFrame buildSensorFrame() {
    SensorFrame frame;
    frame.node_id      = NODE_ID;
    frame.timestamp_ms = millis();
    frame.soil_raw     = g_soil_raw;
    frame.soil_pct     = g_soil_pct;
    frame.temperature  = g_temperature;
    frame.humidity_air = g_humidity_air;
    frame.rain_raw     = g_rain_raw;
    frame.rain_digital = g_rain_digital;
    frame.pump_state   = g_pump_state;
    // CRC tính trên tất cả trừ 1 byte cuối (crc8)
    frame.crc8 = crc8_compute((const uint8_t*)&frame, sizeof(SensorFrame) - 1);
    return frame;
}

// ============================================================
//  ESP-NOW Callbacks
// ============================================================

/**
 * Callback gửi ESP-NOW — log kết quả.
 */
static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("[ESP-NOW] Gửi SensorFrame OK");
    } else {
        Serial.println("[ESP-NOW] Gửi SensorFrame FAIL");
    }
}

/**
 * Callback nhận ESP-NOW — parse CommandFrame từ Master.
 * Kiểm tra CRC trước khi xử lý lệnh.
 */
static void onDataReceived(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    if (data_len != sizeof(CommandFrame)) {
        Serial.printf("[ESP-NOW] Sai kích thước frame: %d\n", data_len);
        return;
    }

    CommandFrame cmd;
    memcpy(&cmd, data, sizeof(CommandFrame));

    // Kiểm tra CRC
    uint8_t calc_crc = crc8_compute((const uint8_t*)&cmd, sizeof(CommandFrame) - 1);
    if (calc_crc != cmd.crc8) {
        Serial.printf("[ESP-NOW] CRC lỗi! Nhận: 0x%02X, Tính: 0x%02X\n", cmd.crc8, calc_crc);
        return;
    }

    // Kiểm tra target
    if (cmd.target_node_id != NODE_ID && cmd.target_node_id != 0xFF) {
        return;  // Không phải cho node này
    }

    Serial.printf("[CMD] Nhận lệnh: 0x%02X, source: %s\n",
                  cmd.command,
                  (cmd.source == SRC_MANUAL_WEB) ? "MANUAL_WEB" : "AI_AUTO");

    if (cmd.command == CMD_ON) {
        // Hard exception: KHÔNG tưới khi đang mưa — dù Master đã check,
        // Nút Biên check lại để an toàn tuyệt đối (defense in depth)
        if (g_rain_digital) {
            Serial.println("[SAFETY] Bị chặn: đang mưa — không thực thi CMD_ON");
            return;
        }
        // RELAY active-LOW: LOW = bơm ON
        // KI-006: nhầm polarity → bơm chạy ngược logic, nguy hiểm
        digitalWrite(RELAY_PIN, LOW);
        g_pump_state = true;
        Serial.println("[RELAY] Bơm BẬT");
    } else if (cmd.command == CMD_OFF) {
        // HIGH = bơm OFF
        digitalWrite(RELAY_PIN, HIGH);
        g_pump_state = false;
        Serial.println("[RELAY] Bơm TẮT");
    } else if (cmd.command == CMD_QUERY) {
        // QUERY: gửi SensorFrame ngay lập tức (không đợi timer)
        SensorFrame frame = buildSensorFrame();
        esp_now_send(g_master_mac, (uint8_t*)&frame, sizeof(SensorFrame));
    }
}

// ============================================================
//  Setup
// ============================================================

void setup() {
    Serial.begin(SERIAL_BAUD);
    Serial.println("\n=== Nút Biên (Edge Actuator) khởi động ===");

    // ---- GPIO Init ----
    // RELAY: OUTPUT, mặc định HIGH = bơm OFF (active-LOW)
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);  // Đảm bảo bơm TẮT khi khởi động

    // Rain DO: INPUT_PULLUP (active-LOW — LOW = mưa)
    pinMode(RAIN_DO_PIN, INPUT_PULLUP);

    // ---- ADC Attenuation ----
    // 11dB → range 0-3.3V (full scale 4095)
    analogSetAttenuation(ADC_11db);
    // GPIO 34, 35 là input-only ADC1 — không cần pinMode

    // ---- DHT11 ----
    dht.setup(DHT_PIN, DHTesp::DHT11);
    Serial.println("[DHT11] Khởi tạo xong");

    // ---- WiFi (Station mode cho ESP-NOW) ----
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.printf("[WiFi] MAC: %s\n", WiFi.macAddress().c_str());

    // ---- ESP-NOW Init ----
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Khởi tạo THẤT BẠI — reboot...");
        ESP.restart();
    }
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceived);

    // ---- Thêm Master peer ----
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, g_master_mac, 6);
    peer_info.channel  = 0;     // Auto channel
    peer_info.encrypt  = false; // TODO: enable AES-128 khi deploy thực tế
    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        Serial.println("[ESP-NOW] Thêm peer THẤT BẠI");
    } else {
        Serial.println("[ESP-NOW] Đã thêm Master peer");
    }

    // ---- Watchdog Timer ----
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);  // panic = true → reboot khi timeout
    esp_task_wdt_add(NULL);                     // đăng ký task hiện tại
    Serial.printf("[WDT] Timeout: %d giây\n", WDT_TIMEOUT_SEC);

    // Đọc cảm biến lần đầu
    readSoilAndRain();
    delay(2000);  // DHT11 cần ổn định sau power-on (chỉ delay() trong setup, không trong loop)
    readDHT();

    Serial.println("[BOOT] Xong — vào vòng lặp chính");
}

// ============================================================
//  Main Loop — Non-blocking, target < 50ms
// ============================================================

void loop() {
    // Feed Watchdog — nếu đoạn nào bị block > 5s, WDT sẽ reboot
    esp_task_wdt_reset();

    uint32_t now = millis();

    // ---- Timer 1: Đọc FC28 + Rain (≥ 2Hz = mỗi 500ms) ----
    if (now - g_last_soil_rain_ms >= SOIL_RAIN_INTERVAL_MS) {
        g_last_soil_rain_ms = now;
        readSoilAndRain();
        Serial.printf("[SENSOR] Soil: raw=%d, pct=%.1f%% | Rain: raw=%d, digital=%s\n",
                      g_soil_raw, g_soil_pct,
                      g_rain_raw, g_rain_digital ? "MƯA" : "KHÔ");
    }

    // ---- Timer 2: Đọc DHT11 (≥ 1Hz = mỗi 1000ms) ----
    if (now - g_last_dht_ms >= DHT_INTERVAL_MS) {
        g_last_dht_ms = now;
        readDHT();
        Serial.printf("[DHT11] Nhiệt độ: %.1f°C | Độ ẩm KK: %.1f%%\n",
                      g_temperature, g_humidity_air);
    }

    // ---- Timer 3: Gửi SensorFrame qua ESP-NOW (mỗi 1000ms) ----
    if (now - g_last_send_ms >= ESPNOW_SEND_INTERVAL_MS) {
        g_last_send_ms = now;
        SensorFrame frame = buildSensorFrame();
        esp_err_t result = esp_now_send(g_master_mac, (uint8_t*)&frame, sizeof(SensorFrame));
        if (result != ESP_OK) {
            Serial.printf("[ESP-NOW] Lỗi gửi: 0x%X\n", result);
        }
    }
}