/**
 * main.cpp — Nút Trung tâm (Master / AI Engine)
 * Board: ESP32-S3 N16R8 (Xiaozhi S3)
 * Vai trò: Điều phối hệ thống, ANN inference, ESP-NOW coordinator,
 *          Web Server SoftAP, LVGL display (TBD hardware).
 *
 * Code Rules:
 *  - Không dùng delay() trong loop — millis()-based non-blocking
 *  - ANN inference < 1ms (FPU hardware LX7)
 *  - Main loop target < 50ms (20Hz)
 *  - Manual Override ưu tiên CAO HƠN AI — nhưng timeout 30 phút
 *  - Hard exceptions (T>38°C, mưa) KHÔNG override được dù manual
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "display.h"
#include "config.h"
#include "protocol.h"
#include "ann.h"
#include "espnow_handler.h"
#include "rtc_handler.h"
#include "web_server.h"

// ============================================================
//  Global State
// ============================================================

static SensorFrame g_frame       = {};  // Frame mới nhất từ Nút Biên
static uint8_t     g_last_cmd    = CMD_OFF;  // Lệnh cuối cùng đã gửi
static bool        g_frame_valid = false;

// Timers
static uint32_t g_last_inference_ms    = 0;
static uint32_t g_last_status_log_ms   = 0;
static uint32_t g_last_display_ms      = 0;

// ============================================================
//  Hàm hỗ trợ
// ============================================================

/**
 * Chạy inference pipeline:
 * 1. Lấy frame mới nhất từ ESP-NOW handler
 * 2. ANN inference (bao gồm hard exceptions)
 * 3. So sánh với lệnh cũ → chỉ gửi nếu thay đổi
 * 4. Cập nhật Web Server state
 */
static void runInferenceCycle() {
    if (!espnow_has_data()) {
        // Chưa nhận được dữ liệu từ Nút Biên
        return;
    }

    g_frame       = espnow_get_latest_frame();
    g_frame_valid = true;

    uint8_t effective_cmd;
    uint8_t source;

    if (web_server_is_manual_override()) {
        // Manual Override — ưu tiên CAO NHẤT
        // Nhưng vẫn kiểm tra hard exceptions (defense in depth)
        uint8_t manual_cmd = web_server_get_manual_cmd();
        if (manual_cmd == CMD_ON) {
            // Hard exception check — tưới khi manual nhưng đang mưa / quá nóng?
            if (g_frame.temperature > TEMP_MAX_WATER) {
                effective_cmd = CMD_OFF;
                Serial.println("[MAIN] MANUAL_ON bị chặn: T>38°C (Thermal Shock)");
            } else if (g_frame.rain_digital) {
                effective_cmd = CMD_OFF;
                Serial.println("[MAIN] MANUAL_ON bị chặn: đang mưa");
            } else {
                effective_cmd = CMD_ON;
            }
        } else {
            effective_cmd = CMD_OFF;
        }
        source = SRC_MANUAL_WEB;
    } else {
        // AUTO mode — dùng ANN inference
        uint8_t hour      = rtc_get_hour();
        float   rain_norm = (float)g_frame.rain_raw / 4095.0f;

        effective_cmd = ann_infer(
            g_frame.soil_pct,
            g_frame.temperature,
            g_frame.humidity_air,
            hour,
            rain_norm,
            g_frame.rain_digital
        );
        source = SRC_AI_AUTO;
    }

    // Chỉ gửi CommandFrame khi trạng thái thay đổi (giảm traffic ESP-NOW)
    if (effective_cmd != g_last_cmd) {
        espnow_send_command(effective_cmd, source, 0);
        g_last_cmd = effective_cmd;
        Serial.printf("[MAIN] Gửi lệnh: %s (source: %s)\n",
                      effective_cmd == CMD_ON ? "ON" : "OFF",
                      source == SRC_MANUAL_WEB ? "MANUAL_WEB" : "AI_AUTO");
    }

    // Cập nhật Web Server với frame mới nhất
    web_server_update(g_frame, effective_cmd, ann_get_last_prob());
}

/**
 * Log trạng thái hệ thống ra Serial mỗi STATUS_LOG_INTERVAL_MS.
 */
static void logStatus() {
    if (!g_frame_valid) {
        Serial.println("[STATUS] Chờ kết nối từ Nút Biên...");
        return;
    }

    Serial.printf("\n========= STATUS [%s] =========\n", rtc_get_time_str());
    Serial.printf("  Soil: raw=%d, pct=%.1f%%\n", g_frame.soil_raw, g_frame.soil_pct);
    Serial.printf("  Temp: %.1f°C | Hum: %.1f%%\n", g_frame.temperature, g_frame.humidity_air);
    Serial.printf("  Rain: raw=%d, digital=%s\n",
                  g_frame.rain_raw, g_frame.rain_digital ? "MƯA" : "KHÔ");
    Serial.printf("  Pump: %s | ANN P=%.3f\n",
                  g_last_cmd == CMD_ON ? "ON" : "OFF", ann_get_last_prob());
    Serial.printf("  Mode: %s",
                  web_server_is_manual_override() ? "MANUAL_OVERRIDE" : "AUTO");
    if (web_server_is_manual_override()) {
        uint32_t rem = web_server_get_override_remaining();
        Serial.printf(" (còn %lu phút %lu giây)", rem / 60, rem % 60);
    }
    Serial.println("\n================================\n");
}

// ============================================================
//  Setup
// ============================================================

void setup() {
    Serial.begin(SERIAL_BAUD);
    Serial.println("\n=== Nút Trung tâm (Master AI) khởi động ===");
    Serial.printf("[INFO] Chip: %s, Rev: %d\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("[INFO] CPU: %d MHz | PSRAM: %d KB\n",
                  ESP.getCpuFreqMHz(), ESP.getPsramSize() / 1024);

    // ---- Wi-Fi SoftAP (phải init TRƯỚC esp_now_init) ----
    // Mode AP_STA: SoftAP phục vụ web, STA dùng cho ESP-NOW
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(SOFTAP_SSID, SOFTAP_PASS);
    Serial.printf("[WiFi] SoftAP: SSID=%s IP=%s\n",
                  SOFTAP_SSID, WiFi.softAPIP().toString().c_str());
    Serial.printf("[WiFi] MAC (STA): %s\n", WiFi.macAddress().c_str());

    // ---- ESP-NOW (sau WiFi.mode) ----
    espnow_init();

    // ---- RTC DS3231 ----
    rtc_init();

    // ---- ANN ----
    ann_init();

    // ---- Web Server ----
    web_server_init();

    // ---- Watchdog ----
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
    Serial.printf("[WDT] Timeout: %d giây\n", WDT_TIMEOUT_SEC);

    // ---- Display LVGL ----
    display_init();

    Serial.println("[BOOT] Xong — vào vòng lặp chính. Kết nối Wi-Fi: " SOFTAP_SSID);
    Serial.println("[BOOT] Dashboard: http://192.168.4.1/");
}

// ============================================================
//  Main Loop — Non-blocking, target < 50ms
// ============================================================

void loop() {
    // Feed Watchdog
    esp_task_wdt_reset();

    uint32_t now = millis();

    // ---- ANN Inference cycle (1Hz) ----
    if (now - g_last_inference_ms >= INFERENCE_INTERVAL_MS) {
        g_last_inference_ms = now;
        runInferenceCycle();
    }

    // ---- Serial status log (mỗi 5 giây) ----
    if (now - g_last_status_log_ms >= STATUS_LOG_INTERVAL_MS) {
        g_last_status_log_ms = now;
        logStatus();
    }

    // ---- LVGL display update (mỗi 1 giây) ----
    if (now - g_last_display_ms >= DISPLAY_REFRESH_MS) {
        g_last_display_ms = now;
        if (g_frame_valid) {
            display_update(g_frame, g_last_cmd, ann_get_last_prob());
        }
    }
    display_task();  // xử lý render queue LVGL, gọi mỗi loop

    // ---- Yield cho AsyncWebServer và background tasks ----
    yield();
}