#pragma once
#include <stdint.h>

// ============================================================
//  RTC Handler — DS3231 qua I²C
//  SDA=GPIO21, SCL=GPIO22, Pull-up 4.7kΩ ngoài
// ============================================================

/**
 * Khởi tạo Wire (I²C) và DS3231.
 * In lỗi nếu RTC không phản hồi (check dây, pull-up).
 * Gọi một lần trong setup().
 */
void rtc_init();

/**
 * Trả về giờ hiện tại (0–23) từ DS3231.
 * Dùng làm x4 cho ANN inference.
 */
uint8_t rtc_get_hour();

/**
 * Trả về chuỗi thời gian định dạng "HH:MM:SS" để hiển thị LCD/Web.
 * Buffer tĩnh nội bộ — không thread-safe, chỉ dùng trong main loop.
 */
const char* rtc_get_time_str();
