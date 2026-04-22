/**
 * display.cpp — LVGL Display Module
 * LCD 2.8" 240×320 | Driver ILI9341 | SPI | DMA + PSRAM frame buffer
 * Board: ESP32-S3 N16R8 (Xiaozhi S3)
 *
 * Kiến trúc:
 *  - Dùng thư viện TFT_eSPI để điều khiển driver ILI9341 qua SPI
 *  - LVGL quản lý giao diện, gọi flush_cb để đẩy dữ liệu qua DMA
 *  - hw_timer ISR tăng lv_tick mỗi 5ms (không chiếm main loop)
 *
 * Màn hình gồm 4 phần:
 *  [Header]  Tiêu đề + thời gian RTC + biểu tượng mode AI/Manual
 *  [Sensors] 4 thẻ cảm biến: Độ ẩm đất, Nhiệt độ, Độ ẩm KK, Mưa
 *  [Status]  Thanh trạng thái bơm + AI probability bar
 *  [Footer]  Uptime + IP SoftAP
 */

#include "display.h"
#include "config.h"
#include "protocol.h"
#include "ann.h"
#include "rtc_handler.h"
#include "web_server.h"

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>

// ============================================================
//  Private — Hardware & LVGL objects
// ============================================================

static TFT_eSPI tft = TFT_eSPI();

// LVGL draw buffer — cấp phát trên PSRAM (KI-004: chỉ qua DMA)
static lv_color_t *s_buf1 = nullptr;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;

// hw_timer cho lv_tick_inc
static hw_timer_t *s_lv_timer = nullptr;

// ---- LVGL UI objects ----------------------------------------
// Header
static lv_obj_t *s_lbl_time   = nullptr;
static lv_obj_t *s_lbl_mode   = nullptr;

// Sensor cards
static lv_obj_t *s_lbl_soil   = nullptr;
static lv_obj_t *s_lbl_temp   = nullptr;
static lv_obj_t *s_lbl_hum    = nullptr;
static lv_obj_t *s_lbl_rain   = nullptr;

// Pump status + AI bar
static lv_obj_t *s_lbl_pump   = nullptr;
static lv_obj_t *s_bar_ai     = nullptr;
static lv_obj_t *s_lbl_ai_pct = nullptr;

// Footer
static lv_obj_t *s_lbl_footer = nullptr;

// ============================================================
//  LVGL Callbacks
// ============================================================

/**
 * flush_cb: LVGL gọi hàm này khi có vùng màn hình cần render.
 * Dùng TFT_eSPI pushImageDMA để đẩy pixel lên LCD qua SPI DMA.
 */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushImage(area->x1, area->y1, w, h, (uint16_t*)color_map);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

/**
 * ISR: hw_timer gọi mỗi LVGL_TICK_MS (5ms) để cập nhật thời gian nội bộ LVGL.
 * Chạy hoàn toàn ngoài main loop → zero latency ảnh hưởng.
 */
static void IRAM_ATTR lvgl_tick_isr() {
    lv_tick_inc(LVGL_TICK_MS);
}

// ============================================================
//  UI Builder
// ============================================================

// Bảng màu Dark Theme — nhất quán với Web Dashboard
#define COLOR_BG        lv_color_hex(0x0f1923)   // Nền tối
#define COLOR_CARD      lv_color_hex(0x1a2635)   // Nền thẻ
#define COLOR_ACCENT    lv_color_hex(0x4fc3f7)   // Xanh nhạt chủ đạo
#define COLOR_SUCCESS   lv_color_hex(0x4caf50)   // Xanh lá (pump ON)
#define COLOR_DANGER    lv_color_hex(0xef5350)   // Đỏ (pump OFF)
#define COLOR_WARN      lv_color_hex(0xff9800)   // Cam (Manual)
#define COLOR_TEXT_DIM  lv_color_hex(0x7a9bb5)   // Chữ mờ

/**
 * Tạo một "thẻ cảm biến" nhỏ: nền card + nhãn đơn vị nhỏ + giá trị lớn.
 * Trả về object label giá trị để cập nhật sau bằng lv_label_set_text().
 */
static lv_obj_t* create_sensor_card(lv_obj_t *parent,
                                     const char *title,
                                     const char *unit,
                                     int x, int y,
                                     int w, int h) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2a3a4a), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 6, 0);

    // Tiêu đề nhỏ phía trên
    lv_obj_t *lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 0);

    // Nhãn giá trị lớn ở giữa
    lv_obj_t *lbl_value = lv_label_create(card);
    lv_label_set_text(lbl_value, "--");
    lv_obj_set_style_text_color(lbl_value, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_value, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_value, LV_ALIGN_CENTER, 0, 4);

    // Đơn vị nhỏ phía dưới
    lv_obj_t *lbl_unit = lv_label_create(card);
    lv_label_set_text(lbl_unit, unit);
    lv_obj_set_style_text_color(lbl_unit, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_unit, LV_ALIGN_BOTTOM_MID, 0, 0);

    return lbl_value;
}

/**
 * Xây dựng toàn bộ giao diện LVGL một lần khi khởi động.
 * Layout (Landscape 240×320 → thực tế 320×240 sau rotation):
 *
 *  ┌────────────────────────────────────────────────────┐ y=0
 *  │  🌱 TuoiTieu AI  │  HH:MM:SS  │  [AI / MAN]       │ h=30 Header
 *  ├──────────┬──────────┬──────────┬────────────────────┤ y=32
 *  │  Đất     │  Nhiệt   │  Ẩm KK   │  Mưa              │ h=68 Cards
 *  │  xx.x %  │  xx.x °C │  xx.x %  │  KHÔ / MƯA        │
 *  ├──────────┴──────────┴──────────┴────────────────────┤ y=102
 *  │  BƠM: [BẬT / TẮT]     AI Confidence: [===] xx%    │ h=50 Status
 *  ├────────────────────────────────────────────────────┤ y=154
 *  │         192.168.4.1  •  Uptime: XXXs               │ h=20 Footer
 *  └────────────────────────────────────────────────────┘ y=176
 */
static void build_ui() {
    // Màn hình chính
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // ---- HEADER ------------------------------------------------
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, LV_PCT(100), 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0d2137), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, LV_SYMBOL_HOME " TuoiTieu AI");
    lv_obj_set_style_text_color(lbl_title, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 4, 0);

    s_lbl_time = lv_label_create(header);
    lv_label_set_text(s_lbl_time, "00:00:00");
    lv_obj_set_style_text_color(s_lbl_time, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_CENTER, 0, 0);

    s_lbl_mode = lv_label_create(header);
    lv_label_set_text(s_lbl_mode, "AI");
    lv_obj_set_style_text_color(s_lbl_mode, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(s_lbl_mode, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_mode, LV_ALIGN_RIGHT_MID, -4, 0);

    // ---- SENSOR CARDS (4 thẻ ngang) ---------------------------
    // Mỗi thẻ rộng 78px, cách nhau 2px, bắt đầu tại y=34
    int card_y = 34;
    int card_h = 68;
    int card_w = 78;
    int gap = 2;

    s_lbl_soil = create_sensor_card(scr, "DO AM DAT", "%",
                                     0*(card_w+gap), card_y, card_w, card_h);
    s_lbl_temp = create_sensor_card(scr, "NHIET DO", "C",
                                     1*(card_w+gap), card_y, card_w, card_h);
    s_lbl_hum  = create_sensor_card(scr, "AM KHI QUYEN", "%",
                                     2*(card_w+gap), card_y, card_w, card_h);
    s_lbl_rain = create_sensor_card(scr, "MUA", "",
                                     3*(card_w+gap), card_y, card_w, card_h);

    // ---- PUMP STATUS + AI BAR ---------------------------------
    int status_y = card_y + card_h + 4;

    lv_obj_t *status_panel = lv_obj_create(scr);
    lv_obj_set_pos(status_panel, 0, status_y);
    lv_obj_set_size(status_panel, LV_PCT(100), 76);
    lv_obj_set_style_bg_color(status_panel, COLOR_CARD, 0);
    lv_obj_set_style_border_width(status_panel, 0, 0);
    lv_obj_set_style_radius(status_panel, 0, 0);
    lv_obj_set_style_pad_all(status_panel, 8, 0);

    // Dòng "BƠM:"
    lv_obj_t *lbl_pump_title = lv_label_create(status_panel);
    lv_label_set_text(lbl_pump_title, "BOM:");
    lv_obj_set_style_text_color(lbl_pump_title, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_pump_title, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_pump_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_pump = lv_label_create(status_panel);
    lv_label_set_text(s_lbl_pump, "TAT");
    lv_obj_set_style_text_color(s_lbl_pump, COLOR_DANGER, 0);
    lv_obj_set_style_text_font(s_lbl_pump, &lv_font_montserrat_16, 0);
    lv_obj_align(s_lbl_pump, LV_ALIGN_TOP_LEFT, 60, 0);

    // Dòng "AI Confidence:"
    lv_obj_t *lbl_ai_title = lv_label_create(status_panel);
    lv_label_set_text(lbl_ai_title, "AI Confidence:");
    lv_obj_set_style_text_color(lbl_ai_title, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_ai_title, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_ai_title, LV_ALIGN_TOP_LEFT, 0, 24);

    s_lbl_ai_pct = lv_label_create(status_panel);
    lv_label_set_text(s_lbl_ai_pct, "0%");
    lv_obj_set_style_text_color(s_lbl_ai_pct, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_ai_pct, &lv_font_montserrat_12, 0);
    lv_obj_align(s_lbl_ai_pct, LV_ALIGN_TOP_RIGHT, 0, 24);

    // Progress bar AI
    s_bar_ai = lv_bar_create(status_panel);
    lv_obj_set_size(s_bar_ai, LV_PCT(100), 10);
    lv_obj_align(s_bar_ai, LV_ALIGN_TOP_MID, 0, 42);
    lv_bar_set_range(s_bar_ai, 0, 100);
    lv_bar_set_value(s_bar_ai, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_ai, lv_color_hex(0x2a3a4a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_ai, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_ai, 5, 0);

    // ---- FOOTER ------------------------------------------------
    int footer_y = status_y + 76 + 2;

    lv_obj_t *footer = lv_obj_create(scr);
    lv_obj_set_pos(footer, 0, footer_y);
    lv_obj_set_size(footer, LV_PCT(100), 22);
    lv_obj_set_style_bg_color(footer, lv_color_hex(0x0d2137), 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 3, 0);

    s_lbl_footer = lv_label_create(footer);
    lv_label_set_text(s_lbl_footer, "192.168.4.1  |  Uptime: 0s");
    lv_obj_set_style_text_color(s_lbl_footer, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_lbl_footer, &lv_font_montserrat_10, 0);
    lv_obj_align(s_lbl_footer, LV_ALIGN_CENTER, 0, 0);
}

// ============================================================
//  Public API
// ============================================================

void display_init() {
    // ---- TFT Init ----
    tft.init();
    tft.setRotation(LCD_ROTATION);
    tft.fillScreen(TFT_BLACK);
    // Bỏ tft.initDMA(); (chạy Safe mode)


    // Backlight ON (nếu mạch có hỗ trợ chân điều khiển BL)
#if LCD_BL >= 0
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
#endif

    Serial.printf("[LCD] TFT_eSPI init OK (%dx%d)\n", LCD_WIDTH, LCD_HEIGHT);

    // ---- LVGL Init ----
    lv_init();

    // Cấp phát draw buffer bằng IRAM (SRAM nội bộ) để loại trừ 100% lỗi OPI PSRAM crash
    size_t buf_size = LCD_WIDTH * LCD_BUF_LINES * sizeof(lv_color_t);
    s_buf1 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL);
    if (!s_buf1) {
        Serial.println("[LCD] THẤT BẠI — không đủ SRAM");
    }
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, nullptr, LCD_WIDTH * LCD_BUF_LINES);

    // Đăng ký display driver
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = LCD_WIDTH;
    s_disp_drv.ver_res  = LCD_HEIGHT;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    // hw_timer ISR cho lv_tick_inc (mỗi 5ms)
    s_lv_timer = timerBegin(0, 80, true);          // timer 0, prescaler 80 (1μs/tick)
    timerAttachInterrupt(s_lv_timer, lvgl_tick_isr, true);
    timerAlarmWrite(s_lv_timer, LVGL_TICK_MS * 1000, true);  // 5000μs = 5ms
    timerAlarmEnable(s_lv_timer);

    // Xây dựng UI
    build_ui();

    Serial.println("[LCD] LVGL init OK — UI rendered");
}

void display_update(const SensorFrame &frame, uint8_t pump_cmd, float ann_prob) {
    char buf[32];

    // ---- Cảm biến ----
    snprintf(buf, sizeof(buf), "%.1f", frame.soil_pct);
    lv_label_set_text(s_lbl_soil, buf);

    snprintf(buf, sizeof(buf), "%.1f", frame.temperature);
    lv_label_set_text(s_lbl_temp, buf);

    snprintf(buf, sizeof(buf), "%.1f", frame.humidity_air);
    lv_label_set_text(s_lbl_hum, buf);

    // Cảm biến mưa: hiện "MUA" (đỏ) hoặc "KHO" (xanh)
    if (frame.rain_digital) {
        lv_label_set_text(s_lbl_rain, "MUA");
        lv_obj_set_style_text_color(s_lbl_rain, lv_color_hex(0x29b6f6), 0);
    } else {
        lv_label_set_text(s_lbl_rain, "KHO");
        lv_obj_set_style_text_color(s_lbl_rain, COLOR_SUCCESS, 0);
    }

    // ---- Trạng thái bơm ----
    if (pump_cmd == CMD_ON) {
        lv_label_set_text(s_lbl_pump, "BAT " LV_SYMBOL_OK);
        lv_obj_set_style_text_color(s_lbl_pump, COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(s_lbl_pump, "TAT " LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(s_lbl_pump, COLOR_DANGER, 0);
    }

    // ---- AI Confidence bar ----
    int pct = (int)(ann_prob * 100.0f);
    lv_bar_set_value(s_bar_ai, pct, LV_ANIM_ON);
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(s_lbl_ai_pct, buf);

    // ---- Mode label ----
    if (web_server_is_manual_override()) {
        lv_label_set_text(s_lbl_mode, "MAN");
        lv_obj_set_style_text_color(s_lbl_mode, COLOR_WARN, 0);
    } else {
        lv_label_set_text(s_lbl_mode, "AI");
        lv_obj_set_style_text_color(s_lbl_mode, COLOR_SUCCESS, 0);
    }

    // ---- Thời gian RTC ----
    lv_label_set_text(s_lbl_time, rtc_get_time_str());

    // ---- Footer: IP + Uptime ----
    snprintf(buf, sizeof(buf), "192.168.4.1  |  %lus", millis() / 1000);
    lv_label_set_text(s_lbl_footer, buf);
}

void display_task() {
    // Gọi trong main loop mỗi LVGL_TASK_MS
    // lv_task_handler() xử lý animation, redraws và flush callbacks
    lv_task_handler();
}
