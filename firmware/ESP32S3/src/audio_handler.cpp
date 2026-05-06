/**
 * audio_handler.cpp — Phase 3: I2S Audio Module
 *
 * Xu ly 2 luong I2S doc lap:
 *   I2S_NUM_0 (RX) — Thu am tu Mic INMP441 / ES8311
 *   I2S_NUM_1 (TX) — Phat am thanh ra Loa qua PA chip
 *
 * Buffer ghi am nam trong PSRAM 8MB (co the luu ~250s mono 16kHz 16-bit).
 * Gioi han thu am: AUDIO_MAX_RECORD_SEC = 10 giay (KI-018).
 *
 * KI-017: PA Enable (IO1) PHAI duoc keo xuong LOW truoc khi phat am thanh.
 *         Neu khong, loa se im lang du I2S dang hoat dong dung.
 */

#include "audio_handler.h"
#include "config.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include "es8311.h"

// ============================================================
//  Private — Hang so & bien trang thai
// ============================================================

// Cau hinh I2S chung cho ca Mic va Loa (Full-Duplex)
#define I2S_PORT        I2S_NUM_0

// Kich thuoc chunk doc moi lan audio_task()
#define I2S_READ_CHUNK  512   // bytes moi lan doc tu DMA

// Buffer ghi am trong PSRAM
static uint8_t  *s_rec_buf     = nullptr;
static size_t    s_rec_len     = 0;
static AudioState s_state      = AUDIO_STATE_IDLE;

// ============================================================
//  Private — Setup I2S ports
// ============================================================

static void setup_i2s_duplex() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate          = AUDIO_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,  // STEREO: ES8311 can frame stereo
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 512,
        .use_apll             = false,  // Khong dung APLL — tranh loi I2S silent
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0       // Khong can MCLK, ES8311 dung BCLK lam clock nguon
    };

    i2s_pin_config_t pins = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,  // ES8311 dung BCLK — khong can MCLK pin
        .bck_io_num   = AUDIO_I2S_BCLK,    // IO5 → ES8311 BCLK
        .ws_io_num    = AUDIO_I2S_WS,      // IO7 → ES8311 LRCK
        .data_out_num = AUDIO_I2S_DOUT,    // IO6 → ES8311 SDIN (phat loa)
        .data_in_num  = AUDIO_I2S_DIN      // IO8 ← ES8311 SDOUT (tu mic)
    };

    esp_err_t ret = i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
    if (ret != ESP_OK) {
        Serial.printf("  [AUDIO] I2S install THAT BAI: 0x%X\n", ret);
        return;
    }
    i2s_set_pin(I2S_PORT, &pins);
    i2s_zero_dma_buffer(I2S_PORT);
    Serial.printf("  [AUDIO] I2S Duplex OK — FS=%dHz, BCLK=IO%d, WS=IO%d\n",
                  AUDIO_SAMPLE_RATE, AUDIO_I2S_BCLK, AUDIO_I2S_WS);
}

// ============================================================
//  Public API
// ============================================================

void audio_init() {
    // KI-017: Cau hinh PA Enable pin, giu HIGH (tat amply) khi idle
    pinMode(AUDIO_PA_ENABLE, OUTPUT);
    digitalWrite(AUDIO_PA_ENABLE, HIGH);  // Amply OFF khi idle tiet kiem dien

    // Cap phat buffer ghi am trong PSRAM
    s_rec_buf = (uint8_t*)heap_caps_malloc(AUDIO_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_rec_buf) {
        Serial.println("[AUDIO] THAT BAI: Khong du PSRAM cho buffer ghi am!");
    } else {
        Serial.printf("[AUDIO] Buffer ghi am: %d KB trong PSRAM\n",
                      AUDIO_BUF_SIZE / 1024);
    }

    // Khoi tao I2S Duplex (1 cong cho ca RX va TX)
    setup_i2s_duplex();

    // Danh thuc chip ES8311 qua I2C (Wire da duoc khoi tao trong rtc_init truoc do)
    es8311_init();

    s_state   = AUDIO_STATE_IDLE;
    s_rec_len = 0;

    Serial.println("[AUDIO] Khoi tao hoan tat — Mic & Loa san sang");
}

void audio_record_start() {
    if (!s_rec_buf) {
        Serial.println("[AUDIO] ERROR: Buffer PSRAM NULL — khong the ghi am");
        return;
    }
    if (s_state == AUDIO_STATE_PLAYING) {
        // Dung phat am thanh neu dang chay
        i2s_zero_dma_buffer(I2S_PORT);
        digitalWrite(AUDIO_PA_ENABLE, HIGH);  // Tat amply
    }

    s_rec_len = 0;
    memset(s_rec_buf, 0, AUDIO_BUF_SIZE);
    i2s_zero_dma_buffer(I2S_PORT);   // Xa buffer cu

    s_state = AUDIO_STATE_RECORDING;
    Serial.printf("[AUDIO] Bat dau ghi am (toi da %ds, %d bytes PSRAM)...\n",
                  AUDIO_MAX_RECORD_SEC, AUDIO_BUF_SIZE);
}

void audio_record_stop() {
    if (s_state != AUDIO_STATE_RECORDING) return;

    s_state = AUDIO_STATE_RECORDED;
    Serial.printf("[AUDIO] Dung ghi am — da luu %d bytes (%.2f giay)\n",
                  s_rec_len,
                  (float)s_rec_len / (AUDIO_SAMPLE_RATE * (AUDIO_BITS / 8)));
}

void audio_play_raw(const int16_t *data, size_t len) {
    if (!data || len == 0) return;

    // Test ca 2 cuc tinh PA_ENABLE
    Serial.printf("  [AUDIO] PA_ENABLE truoc khi phat: IO1=%d\n", digitalRead(AUDIO_PA_ENABLE));
    digitalWrite(AUDIO_PA_ENABLE, LOW);
    delay(50);
    Serial.printf("  [AUDIO] PA_ENABLE sau LOW: IO1=%d (mong muon=0)\n", digitalRead(AUDIO_PA_ENABLE));
    s_state = AUDIO_STATE_PLAYING;

    size_t bytes_written = 0;
    size_t total_written = 0;
    size_t offset = 0;
    const size_t CHUNK = 512;

    while (offset < len) {
        size_t to_write = min(CHUNK, len - offset);
        esp_err_t ret = i2s_write(I2S_PORT,
                  (const uint8_t*)data + offset,
                  to_write,
                  &bytes_written,
                  portMAX_DELAY);
        if (ret != ESP_OK) {
            Serial.printf("  [AUDIO] i2s_write loi: 0x%X\n", ret);
            break;
        }
        offset       += bytes_written;
        total_written += bytes_written;
    }

    Serial.printf("  [AUDIO] i2s_write: gui %d/%d bytes -> %s\n",
                  total_written, len,
                  total_written == len ? "OK" : "THIEU!");

    // Cho DMA phat het buffer roi tat amply
    vTaskDelay(pdMS_TO_TICKS(200));
    digitalWrite(AUDIO_PA_ENABLE, HIGH);  // Tat amply (idle = HIGH)
    s_state = AUDIO_STATE_IDLE;
}

void audio_play_beep() {
    // Song sine 880Hz, 1 giay, amplitude MAX (de nghe ro nhat co the)
    const int FREQ_HZ       = 880;
    const int DURATION_MS   = 1000;
    const int TOTAL_SAMPLES = (AUDIO_SAMPLE_RATE * DURATION_MS) / 1000;
    const int AMPLITUDE     = 32000;  // ~max 16-bit

    int16_t *tone_buf = (int16_t*)malloc(TOTAL_SAMPLES * sizeof(int16_t));
    if (!tone_buf) {
        Serial.println("  [AUDIO] Beep: Khong du RAM!");
        return;
    }

    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        tone_buf[i] = (int16_t)(AMPLITUDE * sinf(2.0f * M_PI * FREQ_HZ * t));
    }

    Serial.printf("  [AUDIO] Beep: %dHz %dms amp=%d\n", FREQ_HZ, DURATION_MS, AMPLITUDE);
    audio_play_raw(tone_buf, TOTAL_SAMPLES * sizeof(int16_t));
    free(tone_buf);
}

AudioState audio_get_state() {
    return s_state;
}

uint8_t* audio_get_record_buf() {
    return s_rec_buf;
}

size_t audio_get_record_len() {
    return s_rec_len;
}

void audio_task() {
    // Goi lien tuc trong loop() — non-blocking
    if (s_state != AUDIO_STATE_RECORDING) return;
    if (!s_rec_buf) return;

    // Da day buffer? Dung ghi am tu dong
    if (s_rec_len >= AUDIO_BUF_SIZE) {
        audio_record_stop();
        Serial.println("[AUDIO] Buffer day — tu dong dung ghi am");
        return;
    }

    // Doc chunk tu I2S DMA vao PSRAM buffer
    size_t bytes_read = 0;
    size_t space_left = AUDIO_BUF_SIZE - s_rec_len;
    size_t to_read    = min((size_t)I2S_READ_CHUNK, space_left);

    esp_err_t ret = i2s_read(I2S_PORT,
                              s_rec_buf + s_rec_len,
                              to_read,
                              &bytes_read,
                              0);  // timeout=0 → non-blocking
    if (ret == ESP_OK && bytes_read > 0) {
        s_rec_len += bytes_read;
    }
}
