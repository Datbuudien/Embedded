#pragma once
#include <stdint.h>
#include "protocol.h"

// ============================================================
//  ESP-NOW Handler — Nút Trung tâm
//  Nhận SensorFrame từ Nút Biên, gửi CommandFrame tới Nút Biên
// ============================================================

/**
 * Khởi tạo Wi-Fi (WIFI_AP_STA) và ESP-NOW.
 * Thêm Edge node peer với MAC từ config.h.
 * Gọi trong setup() TRƯỚC web_server_init().
 */
void espnow_init();

/**
 * Gửi CommandFrame tới Nút Biên qua ESP-NOW.
 *
 * @param cmd          CMD_ON, CMD_OFF, hoặc CMD_QUERY
 * @param source       SRC_AI_AUTO hoặc SRC_MANUAL_WEB
 * @param duration_sec 0 = vô hạn (đến lệnh tiếp)
 */
void espnow_send_command(uint8_t cmd, uint8_t source, uint16_t duration_sec);

/**
 * Trả về bản sao của SensorFrame mới nhất nhận được.
 * Thread-safe (copy atomic với mutex nội bộ).
 */
SensorFrame espnow_get_latest_frame();

/**
 * Kiểm tra xem đã từng nhận được frame từ Nút Biên chưa.
 * Dùng để hiển thị "Chờ kết nối..." trên UI.
 */
bool espnow_has_data();
