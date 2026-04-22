/**
 * rtc_handler.cpp — DS3231 RTC qua I²C
 * SDA=GPIO21, SCL=GPIO22, Pull-up 4.7kΩ ngoài (CR2032 backup)
 */

#include "rtc_handler.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

static RTC_DS3231 rtc;
static bool       s_rtc_ok = false;
static char       s_time_buf[9];  // "HH:MM:SS\0"

void rtc_init() {
    Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);

    if (!rtc.begin()) {
        Serial.println("[RTC] DS3231 KHÔNG phản hồi! Kiểm tra dây SDA/SCL và pull-up 4.7kΩ");
        s_rtc_ok = false;
        return;
    }

    // Nếu RTC mất nguồn backup (CR2032 hết) → reset về thời điểm compile
    if (rtc.lostPower()) {
        Serial.println("[RTC] RTC bị mất nguồn, reset thời gian về thời điểm compile");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    s_rtc_ok = true;
    DateTime now = rtc.now();
    Serial.printf("[RTC] Thời gian hiện tại: %02d:%02d:%02d %02d/%02d/%04d\n",
                  now.hour(), now.minute(), now.second(),
                  now.day(), now.month(), now.year());
}

uint8_t rtc_get_hour() {
    if (!s_rtc_ok) return 12;  // Fallback: giờ trưa (safe default)
    return rtc.now().hour();
}

const char* rtc_get_time_str() {
    if (!s_rtc_ok) {
        return "??:??:??";
    }
    DateTime now = rtc.now();
    snprintf(s_time_buf, sizeof(s_time_buf), "%02d:%02d:%02d",
             now.hour(), now.minute(), now.second());
    return s_time_buf;
}
