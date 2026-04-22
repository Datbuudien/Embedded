#pragma once
#include "protocol.h"

// ============================================================
//  Web Server — AsyncWebServer trên SoftAP
//  SSID: TuoiTieu-AP, Pass: 12345678, IP: 192.168.4.1
// ============================================================

/**
 * Khởi tạo và bắt đầu AsyncWebServer port 80.
 * Đăng ký các route: GET /, GET /status, POST /control.
 * Gọi sau espnow_init() trong setup().
 */
void web_server_init();

/**
 * Gọi trong main loop để:
 *  - Kiểm tra timeout Manual Override (1800s)
 *  - Cập nhật trạng thái sensor để trả về qua /status
 * @param frame    SensorFrame mới nhất từ ESP-NOW
 * @param pump_cmd lệnh bơm hiện tại (CMD_ON / CMD_OFF)
 * @param ann_prob xác suất AI (0.0–1.0)
 */
void web_server_update(const SensorFrame& frame, uint8_t pump_cmd, float ann_prob);

/**
 * Trả về true nếu đang trong Manual Override mode.
 */
bool web_server_is_manual_override();

/**
 * Trả về lệnh bơm từ Manual Override (CMD_ON / CMD_OFF).
 * Chỉ hợp lệ khi web_server_is_manual_override() == true.
 */
uint8_t web_server_get_manual_cmd();

/**
 * Trả về số giây còn lại của Manual Override countdown.
 * 0 nếu không trong manual mode.
 */
uint32_t web_server_get_override_remaining();
