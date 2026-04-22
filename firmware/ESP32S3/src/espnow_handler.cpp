/**
 * espnow_handler.cpp — ESP-NOW communication (Nút Trung tâm)
 * Nhận SensorFrame từ Nút Biên, gửi CommandFrame tới Nút Biên.
 * Dùng portMUX để bảo vệ g_latest_frame (callback chạy ở ISR context).
 */

#include "espnow_handler.h"
#include "config.h"
#include "protocol.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// ---- Private State -----------------------------------------
static SensorFrame g_latest_frame  = {};
static bool        g_has_data      = false;
static portMUX_TYPE g_mux          = portMUX_INITIALIZER_UNLOCKED;

// MAC của Nút Biên (Edge node 0)
static uint8_t g_edge_mac[] = EDGE_MAC_0;

// ---- Callbacks ---------------------------------------------

static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.printf("[ESP-NOW] Send CommandFrame: %s\n",
                  status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

static void onDataReceived(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    if (data_len != (int)sizeof(SensorFrame)) {
        Serial.printf("[ESP-NOW] Sai kích thước frame: %d (expected %d)\n",
                      data_len, sizeof(SensorFrame));
        return;
    }

    SensorFrame frame;
    memcpy(&frame, data, sizeof(SensorFrame));

    // Kiểm tra CRC
    uint8_t calc_crc = crc8_compute((const uint8_t*)&frame, sizeof(SensorFrame) - 1);
    if (calc_crc != frame.crc8) {
        Serial.printf("[ESP-NOW] CRC lỗi! Nhận: 0x%02X, Tính: 0x%02X\n",
                      frame.crc8, calc_crc);
        return;
    }

    // Cập nhật frame toàn cục (thread-safe với portMUX)
    portENTER_CRITICAL(&g_mux);
    memcpy(&g_latest_frame, &frame, sizeof(SensorFrame));
    g_has_data = true;
    portEXIT_CRITICAL(&g_mux);

    Serial.printf("[ESP-NOW] Nhận SensorFrame node=%d | soil=%.1f%% T=%.1f°C H=%.1f%% rain=%s pump=%s\n",
                  frame.node_id, frame.soil_pct, frame.temperature, frame.humidity_air,
                  frame.rain_digital ? "MƯA" : "KHÔ",
                  frame.pump_state  ? "ON"  : "OFF");
}

// ---- Implementation ----------------------------------------

void espnow_init() {
    // WiFi mode AP+STA: SoftAP cho web server, STA cho ESP-NOW
    // esp_now_init() yêu cầu WiFi đã được init trước
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Khởi tạo THẤT BẠI — reboot...");
        ESP.restart();
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceived);

    // Thêm Nút Biên peer
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, g_edge_mac, 6);
    peer_info.channel = 0;
    peer_info.encrypt = false; // TODO: AES-128 khi deploy thực tế

    if (esp_now_add_peer(&peer_info) == ESP_OK) {
        Serial.printf("[ESP-NOW] Đã thêm Edge peer: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      g_edge_mac[0], g_edge_mac[1], g_edge_mac[2],
                      g_edge_mac[3], g_edge_mac[4], g_edge_mac[5]);
    } else {
        Serial.println("[ESP-NOW] Thêm peer THẤT BẠI — kiểm tra MAC");
    }
}

void espnow_send_command(uint8_t cmd, uint8_t source, uint16_t duration_sec) {
    CommandFrame frame;
    frame.target_node_id = 0;          // Node 0 = Nút Biên đầu tiên
    frame.command        = cmd;
    frame.source         = source;
    frame.duration_sec   = duration_sec;
    frame.crc8           = crc8_compute((const uint8_t*)&frame, sizeof(CommandFrame) - 1);

    esp_err_t result = esp_now_send(g_edge_mac, (uint8_t*)&frame, sizeof(CommandFrame));
    if (result != ESP_OK) {
        Serial.printf("[ESP-NOW] Lỗi gửi CommandFrame: 0x%X\n", result);
    }
}

SensorFrame espnow_get_latest_frame() {
    SensorFrame copy;
    portENTER_CRITICAL(&g_mux);
    memcpy(&copy, &g_latest_frame, sizeof(SensorFrame));
    portEXIT_CRITICAL(&g_mux);
    return copy;
}

bool espnow_has_data() {
    return g_has_data;
}
