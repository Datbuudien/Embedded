#pragma once

// ============================================================
//  Display Config — LCD 2.8" 240×320 SPI (Driver ILI9341)
//  Board: ESP32-S3 N16R8 (Xiaozhi S3)
//  Dùng PSRAM 8MB làm frame buffer, DMA flush để không block CPU
// ============================================================

// ---- Chân SPI của LCD 2.8" (ES3C28P/ES3N28P) ---------------
#define LCD_MOSI    11      // SPI Data (MOSI)
#define LCD_MISO    13      // SPI Data Out (MISO)
#define LCD_SCLK    12      // SPI Clock
#define LCD_CS      10      // Chip Select
#define LCD_DC      46      // Data/Command
#define LCD_RST     -1      // Chia sẻ với nút Reset tổng (EN)
#define LCD_BL      45      // Backlight (HIGH = Bật)

// ---- Thông số màn hình ------------------------------------
#define LCD_WIDTH    240
#define LCD_HEIGHT   320
#define LCD_ROTATION   1    // 0=Portrait, 1=Landscape 90°, 2=Portrait 180°, 3=Landscape 270°

// ---- LVGL Buffer -------------------------------------------
// Dùng PSRAM — phải cấp phát heap, không dùng static array lớn trên stack
// 1 buffer = 240×40 pixels × 2 bytes/pixel = 19.2KB (đủ nhẹ, đủ mượt)
#define LCD_BUF_LINES  40   // Số dòng mỗi lần DMA flush

// ---- LVGL Refresh Rate ------------------------------------
#define LVGL_TICK_MS    5   // Gọi lv_tick_inc() mỗi 5ms (qua hw_timer)
#define LVGL_TASK_MS   10   // Gọi lv_task_handler() mỗi 10ms trong loop

#include "protocol.h"

void display_init();
void display_update(const SensorFrame &frame, uint8_t pump_cmd, float ann_prob);
void display_task();
