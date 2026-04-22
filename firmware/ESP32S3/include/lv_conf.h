/**
 * lv_conf.h — LVGL Configuration
 * Bật các tính năng tối thiểu để build thành công và hiển thị được UI.
 * Dựa trên template lv_conf_template.h của LVGL v8.
 */
#if 1  /* Set to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Màu sắc — 16-bit RGB565 (phổ biến nhất cho ILI9341) */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Kích thước bộ nhớ heap nội bộ LVGL */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64 * 1024U)   /* 64KB heap nội bộ */

/* HAL tick — cung cấp bởi hw_timer ISR trong display.cpp */
#define LV_TICK_CUSTOM 0

/* Logging — bật để debug */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Performance */
#define LV_DEF_REFR_PERIOD 10      /* Chu kỳ refresh mặc định (ms) */
#define LV_DPI_DEF 130             /* DPI ước tính của màn 2.8 inch */

/* Drawing */
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4
#define LV_IMG_CACHE_DEF_SIZE 0
#define LV_GRADIENT_MAX_STOPS 2
#define LV_GRAD_CACHE_DEF_SIZE 0

/* Animation */
#define LV_USE_ANIMATION 1

/* Scroll */
#define LV_USE_SCROLL_ANIM 1

/* Fonts */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Text */
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_COLOR_CMD "#"
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/* Widgets bật tối thiểu */
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 1
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_LABEL_TEXT_SELECTION 1
#define LV_LABEL_LONG_TXT_HINT 1
#define LV_USE_LINE 1
#define LV_USE_ROLLER 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 1

/* Themes */
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_BASIC 1
#define LV_USE_THEME_MONO 1

/* Layouts */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* 3rd party */
#define LV_USE_FS_STDIO 0
#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_RLOTTIE 0
#define LV_USE_FFMPEG 0

/* Đặt symbol cơ bản */
#define LV_USE_BUILTIN_MALLOC 1
#define LV_USE_BUILTIN_MEMCPY 1

#endif /* LV_CONF_H */
#endif /* 1 */
