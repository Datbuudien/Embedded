/**
 * main.cpp — Nut Trung tam (Master / AI Engine)
 * Board: ESP32-S3 N16R8 (Xiaozhi S3)
 *
 * Phase 2: ANN multi-layer INT8 (weights_q8.h) + ESP-NOW coordinator
 * Phase 3: Audio I2S — Nut BOOT bat/tat ghi am, test loa
 *
 * Code Rules:
 *  - Khong dung delay() trong loop — millis()-based non-blocking
 *  - ANN inference < 1ms (FPU hardware LX7 + INT8 quantized)
 *  - Main loop target < 50ms (20Hz)
 *  - Manual Override uu tien CAO HON AI — nhung timeout 30 phut
 *  - Hard exceptions (T>38C, mua) KHONG override duoc du manual
 *  - BOOT button (IO0): nhan → ghi am, nha → xu ly audio
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
#include "audio_handler.h"
#include "es8311.h"

// ============================================================
//  Global State
// ============================================================

static SensorFrame g_frame       = {};
static uint8_t     g_last_cmd    = CMD_OFF;
static bool        g_frame_valid = false;

// Timers
static uint32_t g_last_inference_ms    = 0;
static uint32_t g_last_status_log_ms   = 0;
static uint32_t g_last_display_ms      = 0;

// ============================================================
//  BOOT Button — Phase 3
//  IO0: BOOT button (active-LOW, co internal pull-up)
//  Nhan → bat dau ghi am
//  Nha  → dung ghi am, chuan bi xu ly
// ============================================================

#define BOOT_BTN_PIN     0
#define BOOT_DEBOUNCE_MS 50

static bool     s_btn_last_state  = HIGH;  // Released
static uint32_t s_btn_last_ms     = 0;

/**
 * Kiem tra BOOT button, quan ly state machine ghi am.
 * Goi trong moi iteration cua loop().
 */
static void handle_boot_button() {
    bool btn_now = digitalRead(BOOT_BTN_PIN);
    uint32_t now = millis();

    // Debounce
    if (btn_now == s_btn_last_state) return;
    if (now - s_btn_last_ms < BOOT_DEBOUNCE_MS) return;

    s_btn_last_state = btn_now;
    s_btn_last_ms    = now;

    if (btn_now == LOW) {
        Serial.println("[BTN] BOOT nhan - Bat dau ghi am (noi vao mic)...");
        audio_record_start();
    } else {
        audio_record_stop();
        size_t   rec_len   = audio_get_record_len();
        int16_t *buf       = (int16_t*)audio_get_record_buf();
        size_t   n_samples = rec_len / 2;

        Serial.printf("\n======= MIC TEST =======\n");
        Serial.printf("  Da ghi : %d bytes = %d samples (%.2fs)\n",
                      rec_len, n_samples, (float)rec_len / (16000 * 2));

        if (n_samples > 0 && buf != nullptr) {
            int16_t  val_min  = 32767;
            int16_t  val_max  = -32768;
            long long sum_sq  = 0;
            uint32_t nonzero  = 0;
            for (size_t i = 0; i < n_samples; i++) {
                int16_t s = buf[i];
                if (s < val_min) val_min = s;
                if (s > val_max) val_max = s;
                sum_sq += (long long)s * s;
                if (s != 0) nonzero++;
            }
            float rms         = sqrtf((float)(sum_sq / n_samples));
            float pct_nonzero = 100.0f * nonzero / n_samples;
            Serial.printf("  Min      : %d\n", val_min);
            Serial.printf("  Max      : %d\n", val_max);
            Serial.printf("  Bien do  : %d\n", (int)val_max - val_min);
            Serial.printf("  RMS      : %.1f  (>500=co tieng)\n", rms);
            Serial.printf("  Non-zero : %.1f%%  (>50%%=mic OK)\n", pct_nonzero);
            if (pct_nonzero < 1.0f)
                Serial.println("  => TOAN ZERO - MIC KHONG hoat dong! Kiem tra IO8,IO5,IO7");
            else if (rms < 200.0f)
                Serial.println("  => Co du lieu nhung RMS thap - Mic yeu, thu noi to hon");
            else
                Serial.printf("  => MIC HOAT DONG OK! RMS=%.0f\n", rms);
        } else {
            Serial.println("  LOI: Khong co du lieu ghi am!");
        }
        Serial.println("========================\n");
        // TODO Phase 4: whisper_upload(buf, rec_len)
    }
}

// ============================================================
//  Phase 2: ANN Inference Pipeline
// ============================================================

/**
 * Chay inference pipeline:
 * 1. Lay frame moi nhat tu ESP-NOW handler
 * 2. ANN inference INT8 multi-layer (bao gom hard exceptions)
 * 3. So sanh voi lenh cu → chi gui neu thay doi
 * 4. Cap nhat Web Server state
 */
static void runInferenceCycle() {
    if (!espnow_has_data()) {
        // Chua nhan duoc du lieu tu Nut Bien
        return;
    }

    // Tam dung inference neu dang ghi am (tranh nhieu Serial log)
    if (audio_get_state() == AUDIO_STATE_RECORDING) return;

    g_frame       = espnow_get_latest_frame();
    g_frame_valid = true;

    uint8_t effective_cmd;
    uint8_t source;

    if (web_server_is_manual_override()) {
        // Manual Override — uu tien CAO NHAT
        // Nhung van kiem tra hard exceptions (defense in depth)
        uint8_t manual_cmd = web_server_get_manual_cmd();
        if (manual_cmd == CMD_ON) {
            if (g_frame.temperature > TEMP_MAX_WATER) {
                effective_cmd = CMD_OFF;
                Serial.println("[MAIN] MANUAL_ON bi chan: T>38C (Thermal Shock)");
            } else if (g_frame.rain_digital) {
                effective_cmd = CMD_OFF;
                Serial.println("[MAIN] MANUAL_ON bi chan: dang mua");
            } else {
                effective_cmd = CMD_ON;
            }
        } else {
            effective_cmd = CMD_OFF;
        }
        source = SRC_MANUAL_WEB;
    } else {
        // AUTO mode — dung ANN inference INT8 (Phase 2)
        uint8_t hour = rtc_get_hour();
        effective_cmd = ann_infer(
            g_frame.soil_pct,
            g_frame.temperature,
            g_frame.humidity_air,
            hour,
            g_frame.rain_digital
        );
        source = SRC_AI_AUTO;
    }

    // Chi gui CommandFrame khi trang thai thay doi (giam traffic ESP-NOW)
    if (effective_cmd != g_last_cmd) {
        espnow_send_command(effective_cmd, source, 0);
        g_last_cmd = effective_cmd;
        Serial.printf("[MAIN] Gui lenh: %s (source: %s)\n",
                      effective_cmd == CMD_ON ? "ON" : "OFF",
                      source == SRC_MANUAL_WEB ? "MANUAL_WEB" : "AI_AUTO");
    }

    // Cap nhat Web Server voi frame moi nhat
    web_server_update(g_frame, effective_cmd, ann_get_last_prob());
}

/**
 * Log trang thai he thong ra Serial moi STATUS_LOG_INTERVAL_MS.
 */
static void logStatus() {
    if (audio_get_state() == AUDIO_STATE_RECORDING) return;

    const char* audio_str[] = {"IDLE", "RECORDING", "RECORDED", "PLAYING"};
    const char* mode_str = web_server_is_manual_override() ? "MANUAL" : "AUTO-AI";

    Serial.println("\n--- STATUS ---");
    Serial.printf("  Time  : %s | Uptime=%lus\n", rtc_get_time_str(), millis() / 1000);
    if (g_frame_valid) {
        Serial.printf("  Sensor: Dat=%.1f%% Nhiet=%.1fC Am=%.1f%% Mua=%s\n",
                      g_frame.soil_pct, g_frame.temperature,
                      g_frame.humidity_air, g_frame.rain_digital ? "CO" : "KHO");
        Serial.printf("  AI    : P=%.3f Bom=%s Mode=%s\n",
                      ann_get_last_prob(),
                      g_last_cmd == CMD_ON ? "BAT" : "TAT", mode_str);
    } else {
        Serial.println("  Sensor: Cho Nut Bien ket noi...");
    }
    Serial.printf("  Audio : %s | PSRAM=%dKB | Heap=%dKB\n",
                  audio_str[audio_get_state()],
                  ESP.getPsramSize() / 1024,
                  ESP.getFreeHeap() / 1024);
    Serial.printf("  ES8311: %s\n",
                  es8311_check() ? "OK (I2C 0x18 phan hoi)" : "LOI! Khong tim thay chip");
    // PA_ENABLE = IO1, active-LOW (LOW=bat amply)
    Serial.printf("  PA_EN : IO1 hien dang = %s\n",
                  digitalRead(1) == LOW ? "LOW (Amply BAT)" : "HIGH (Amply TAT)");
    Serial.println("--------------");

    // Auto-test: phat beep moi 9 giay (3 lan STATUS = 1 lan beep)
    static uint8_t s_beep_counter = 0;
    s_beep_counter++;
    if (s_beep_counter >= 3) {
        s_beep_counter = 0;
        if (audio_get_state() == AUDIO_STATE_IDLE) {
            Serial.println("  >> Auto-beep test loa...");
            audio_play_beep();
            Serial.println("  >> Xong. Neu khong nghe thi PA_ENABLE sai cuc tinh!");
        }
    }
}


// ============================================================
//  Setup
// ============================================================

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);  // Cho Serial Monitor ket noi truoc khi in

    Serial.println("\n\n======================================");
    Serial.println("  NUT TRUNG TAM (Master AI) KHOI DONG");
    Serial.println("======================================");
    Serial.printf("[INFO] Chip : %s, Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("[INFO] CPU  : %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("[INFO] Flash: %d MB\n", ESP.getFlashChipSize() / (1024*1024));
    Serial.printf("[INFO] PSRAM: %d KB\n", ESP.getPsramSize() / 1024);
    Serial.flush();
    delay(300);

    // ---- BOOT Button ---
    Serial.println("\n[1/7] BOOT Button...");
    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
    Serial.println("  OK — IO0, Nhan giu de ghi am");
    Serial.flush();
    delay(200);

    // ---- Wi-Fi SoftAP (phai init TRUOC esp_now_init) ----
    Serial.println("\n[2/7] Wi-Fi SoftAP...");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(SOFTAP_SSID, SOFTAP_PASS);
    Serial.printf("  OK — SSID : %s\n", SOFTAP_SSID);
    Serial.printf("  OK — IP   : %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("  OK — MAC  : %s\n", WiFi.macAddress().c_str());
    Serial.flush();
    delay(200);

    // ---- ESP-NOW (sau WiFi.mode) ----
    Serial.println("\n[3/7] ESP-NOW...");
    espnow_init();
    Serial.flush();
    delay(200);

    // ---- RTC DS3231 ----
    Serial.println("\n[4/7] RTC DS3231 (I2C SDA=16 SCL=15)...");
    rtc_init();
    Serial.flush();
    delay(200);

    // ---- ANN (Phase 2: INT8 multi-layer) ----
    Serial.println("\n[5/7] ANN (INT8 Quantized)...");
    ann_init();
    Serial.flush();
    delay(200);

    // ---- Audio I2S (Phase 3) ----
    Serial.println("\n[6/7] Audio I2S + ES8311 Codec...");
    audio_init();
    Serial.flush();
    delay(500);  // Doi ES8311 on dinh sau khi cap nguon

    // Test loa ngay khi boot — beep 1 lan de xac nhan loa hoat dong
    Serial.println("  >> Test loa: Phat beep 880Hz (0.3s)...");
    Serial.flush();
    audio_play_beep();
    Serial.println("  >> Beep xong — neu nghe thay thi LOA OK!");
    Serial.flush();
    delay(300);

    // ---- Web Server ----
    Serial.println("\n[7/7] Web Server...");
    web_server_init();
    Serial.flush();
    delay(200);

    // ---- Watchdog ----
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);

    Serial.printf("[WDT] Timeout: %d giay\n", WDT_TIMEOUT_SEC);

    // ---- Display LVGL ----
    display_init();

    Serial.println("[BOOT] Xong — vao vong lap chinh.");
    Serial.println("[BOOT] Dashboard: http://192.168.4.1/");
    Serial.println("[BOOT] Nhan BOOT (IO0) de ghi am giong noi.");
}

// ============================================================
//  Main Loop — Non-blocking, target < 50ms
// ============================================================

void loop() {
    // Feed Watchdog
    esp_task_wdt_reset();

    uint32_t now = millis();

    // ---- BOOT Button (Phase 3) — kiem tra moi iteration ----
    handle_boot_button();

    // ---- Audio I2S task (Phase 3) — doc mic vao PSRAM ----
    audio_task();

    // ---- ANN Inference cycle (Phase 2, 1Hz) ----
    if (now - g_last_inference_ms >= INFERENCE_INTERVAL_MS) {
        g_last_inference_ms = now;
        runInferenceCycle();
    }

    // ---- Serial status log (moi 3 giay, de theo doi) ----
    if (now - g_last_status_log_ms >= 3000) {
        g_last_status_log_ms = now;
        logStatus();
    }

    // ---- LVGL display update (moi 1 giay) ----
    if (now - g_last_display_ms >= DISPLAY_REFRESH_MS) {
        g_last_display_ms = now;
        if (g_frame_valid) {
            display_update(g_frame, g_last_cmd, ann_get_last_prob());
        }
    }
    display_task();  // Xu ly render queue LVGL, goi moi loop

    // ---- Yield cho AsyncWebServer va background tasks ----
    yield();
}