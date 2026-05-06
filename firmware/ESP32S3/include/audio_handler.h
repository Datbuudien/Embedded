#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================
//  audio_handler.h — Phase 3: I2S Audio (Mic & Speaker)
//  Board: Xiaozhi S3 / ES3N28P
//
//  Cau hinh chan (tu config.h):
//    PA_ENABLE = IO1   (LOW = bat mach amply)
//    MCLK      = IO4
//    BCLK      = IO5
//    DOUT      = IO6   (Data ra Loa)
//    WS/LRC    = IO7
//    DIN       = IO8   (Data vao tu Mic)
//
//  Kich thuoc buffer: PSRAM 8MB du luu ~250s audio 16kHz mono 16-bit
//  Gioi han thu am:  10 giay (KI-018: Whisper API < 25MB)
// ============================================================

// Thong so I2S
#define AUDIO_SAMPLE_RATE    16000    // 16kHz — chuan Whisper
#define AUDIO_BITS           16       // 16-bit PCM
#define AUDIO_CHANNELS       1        // Mono
#define AUDIO_MAX_RECORD_SEC 10       // Toi da 10 giay (KI-018)

// Kich thuoc buffer ghi am trong PSRAM
// 16000 samples/s * 2 bytes/sample * 10s = 320.000 bytes ~ 312KB
#define AUDIO_BUF_SIZE  (AUDIO_SAMPLE_RATE * (AUDIO_BITS/8) * AUDIO_MAX_RECORD_SEC)

// ---- State enum ----
typedef enum {
    AUDIO_STATE_IDLE = 0,
    AUDIO_STATE_RECORDING,
    AUDIO_STATE_RECORDED,   // Co du lieu, san sang xu ly
    AUDIO_STATE_PLAYING
} AudioState;

/**
 * Khoi tao I2S cho ca Mic (I2S_NUM_0) va Loa (I2S_NUM_1).
 * Cap phat buffer PSRAM cho ghi am.
 * Goi mot lan trong setup().
 */
void audio_init();

/**
 * Bat dau ghi am tu Mic.
 * - Tat loa neu dang phat
 * - Reset buffer ghi am
 * - Thay doi state → AUDIO_STATE_RECORDING
 */
void audio_record_start();

/**
 * Dung ghi am.
 * - Luu so byte da ghi
 * - Thay doi state → AUDIO_STATE_RECORDED
 * Goi khi nguoi dung nha nut BOOT.
 */
void audio_record_stop();

/**
 * Phat du lieu audio tu pointer (dung cho test tone hoac MP3 da giai ma).
 * @param data     Con tro den buffer PCM 16-bit
 * @param len      So byte can phat
 * Khi ket thuc: state → AUDIO_STATE_IDLE
 */
void audio_play_raw(const int16_t *data, size_t len);

/**
 * Phat tieng bip ngan de test loa.
 * Khi ket thuc: state → AUDIO_STATE_IDLE
 */
void audio_play_beep();

/**
 * Lay state hien tai.
 */
AudioState audio_get_state();

/**
 * Con tro den buffer ghi am trong PSRAM.
 * Chi hop le khi state == AUDIO_STATE_RECORDED.
 */
uint8_t* audio_get_record_buf();

/**
 * So byte da ghi am (= kich thuoc du lieu upload Whisper).
 */
size_t audio_get_record_len();

/**
 * Goi trong loop() de xu ly AUDIO_STATE_RECORDING:
 * doc du lieu tu I2S DMA vao PSRAM buffer theo tung chunk.
 * Phai goi lien tuc (non-blocking), khong delay().
 */
void audio_task();
