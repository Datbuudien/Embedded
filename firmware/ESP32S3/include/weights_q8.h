// ================================================================
//  weights_q8.h  –  Trọng số INT8 (Quantized)
//  Auto-generated bởi quantize_weights.py
//  Kiến trúc: Input(4) -> Hidden(8, ReLU) -> Output(1, Sigmoid)
//
//  Phương pháp: Symmetric INT8 cho W, Asymmetric INT8 cho bias
// ================================================================
#pragma once
#include <stdint.h>
#include <math.h>

// ── Kích thước ───────────────────────────────────────────────
#define Q8_N_INPUT   4
#define Q8_N_HIDDEN  8
#define Q8_N_OUTPUT  1
#define Q8_THRESHOLD 0.50f

// ── Chuẩn hóa đầu vào (dùng chung với float32) ──────────────
static const float Q8_SCALE_MIN[4] = { 10.0000f, 20.0000f, 40.0000f, 0.0000f };
static const float Q8_SCALE_MAX[4] = { 90.0000f, 40.0000f, 90.0000f, 24.0000f };

// ── Tham số lượng tử hóa ─────────────────────────────────────
#define Q8_SCALE_W1  0.00843168f
#define Q8_ZP_W1     0
#define Q8_SCALE_W2  0.00343082f
#define Q8_ZP_W2     0
#define Q8_SCALE_B1  0.00077221f
#define Q8_ZP_B1     -19
#define Q8_SCALE_B2  1.00000000f
#define Q8_ZP_B2     0

// ── Trọng số INT8 ─────────────────────────────────────────────
static const int8_t Q8_W1[8][4] = {
  18, -62, 45, 56, -116, -77, 8, -19, -1, -51, 52, 46, 4, 67, 28, -51,
  22, -57, 52, -3, -11, -40, 72, -9, -25, -21, 32, 22, 24, 26, 127, -24,
};

static const int8_t Q8_b1[8] = { -85, -124, 61, 127, -34, -128, -126, 65 };

static const int8_t Q8_W2[1][8] = {
  108, 79, -97, 34, 17, 32, 127, 33,
};

static const int8_t Q8_b2[1] = { 0 };

// ================================================================
//  HÀM SUY LUẬN INT8 – nhanh hơn float32 ~3x trên ESP32-S3
// ================================================================
static inline float _q8_norm(float val, int idx) {
    float range = Q8_SCALE_MAX[idx] - Q8_SCALE_MIN[idx];
    if (range < 1e-6f) return 0.0f;
    float n = (val - Q8_SCALE_MIN[idx]) / range;
    return (n < 0.f) ? 0.f : (n > 1.f ? 1.f : n);
}

float ANN_infer_q8(float do_am_dat, float nhiet_do,
                   float do_am_kk,  float gio) {
    // Chuẩn hóa (vẫn dùng float, nhẹ hơn inference)
    float x[Q8_N_INPUT] = {
        _q8_norm(do_am_dat, 0),
        _q8_norm(nhiet_do,  1),
        _q8_norm(do_am_kk,  2),
        _q8_norm(gio,       3),
    };

    // Lượng tử hóa đầu vào -> int16 (tránh overflow khi cộng dồn)
    int16_t xq[Q8_N_INPUT];
    for (int j = 0; j < Q8_N_INPUT; j++)
        xq[j] = (int16_t)(x[j] * 127);

    // Lớp ẩn: dot product int8 x int8 -> accumulate int32, dequant, ReLU
    float h[Q8_N_HIDDEN];
    for (int i = 0; i < Q8_N_HIDDEN; i++) {
        int32_t acc = (int32_t)(Q8_b1[i] - Q8_ZP_B1) * 16384; // scale bias
        for (int j = 0; j < Q8_N_INPUT; j++)
            acc += (int32_t)(Q8_W1[i][j] - Q8_ZP_W1) * xq[j];
        // De-quantize và ReLU
        float z = (float)acc * (Q8_SCALE_W1 / 127.0f) + Q8_SCALE_B1 * (Q8_b1[i] - Q8_ZP_B1);
        h[i] = (z > 0.0f) ? z : 0.0f;
    }

    // Lớp output (float để đảm bảo độ chính xác sigmoid)
    float z2 = Q8_SCALE_B2 * (Q8_b2[0] - Q8_ZP_B2);
    for (int i = 0; i < Q8_N_HIDDEN; i++)
        z2 += Q8_SCALE_W2 * (Q8_W2[0][i] - Q8_ZP_W2) * h[i];

    return 1.0f / (1.0f + expf(-z2));
}
