// ================================================================
//  weights_export.h  –  Auto-generated bởi train_irrigation_model.py
//  KHÔNG chỉnh sửa thủ công.
//  Kiến trúc: Input(4) -> Hidden(8, ReLU) -> Output(1, Sigmoid)
// ================================================================
#pragma once
#include <math.h>

// ── Kích thước ───────────────────────────────────────────────
#define ANN_N_INPUT   4
#define ANN_N_HIDDEN  8
#define ANN_N_OUTPUT  1
#define ANN_THRESHOLD 0.50f

// ── Tham số chuẩn hóa MinMax (từ tập train) ─────────────────
//    X_norm = (X - X_min) / (X_max - X_min)
static const float SCALE_MIN[4] = { 10.0000f, 20.0000f, 40.0000f, 0.0000f };
static const float SCALE_MAX[4] = { 90.0000f, 40.0000f, 90.0000f, 24.0000f };

// ── Trọng số lớp ẩn W1[8][4] ──────────────────────────
static const float W1[8][4] = {
  1.47192371f, 0.64547333f, 0.33923152f, 7.08662110f, -1.30413045f, -0.87041585f, 0.08545236f, -0.21138604f,
  0.61524009f, 0.85689704f, 1.16824253f, -0.50507372f, -0.09928297f, 1.06980013f, 0.52739679f, -0.54662992f,
  3.73288453f, -1.46631463f, 0.91726116f, 0.88975926f, 0.00703334f, 0.32465003f, 0.66857839f, -6.74599713f,
  -0.82677231f, 0.20418796f, 0.21120215f, 4.79447806f, 0.55057417f, 2.10968495f, 1.66866645f, -2.33612884f,
};

// ── Bias lớp ẩn b1[8] ────────────────────────────────────
static const float b1[8] = { -2.98638265f, 0.00000000f, 2.76580029f, 1.01139725f, -1.14710569f, 4.99197219f, -4.05507267f, -1.58383025f };

// ── Trọng số lớp output W2[1][8] ──────────────────────
static const float W2[1][8] = {
  -2.79487704f, -0.38463081f, 2.56866465f, 1.09816201f, -1.90970116f, -3.27015880f, -5.62183530f, -2.15675842f,
};

// ── Bias lớp output b2[1] ─────────────────────────────────
static const float b2[1] = { 3.00612641f };

// ================================================================
//  HÀM SUY LUẬN (inline, gọi từ module AI trong Arduino)
// ================================================================
inline float ann_normalize(float val, int idx) {
    float range = SCALE_MAX[idx] - SCALE_MIN[idx];
    if (range < 1e-6f) return 0.0f;
    float norm = (val - SCALE_MIN[idx]) / range;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    return norm;
}

inline float ann_run(float do_am_dat, float nhiet_do,
                     float do_am_kk,  float gio) {
    // Đầu vào thô -> chuẩn hóa
    float x[ANN_N_INPUT] = {
        ann_normalize(do_am_dat, 0),
        ann_normalize(nhiet_do,  1),
        ann_normalize(do_am_kk,  2),
        ann_normalize(gio,       3),
    };

    // Lớp ẩn (ReLU)
    float h[ANN_N_HIDDEN];
    for (int i = 0; i < ANN_N_HIDDEN; i++) {
        float z = b1[i];
        for (int j = 0; j < ANN_N_INPUT; j++) z += W1[i][j] * x[j];
        h[i] = (z > 0.0f) ? z : 0.0f;   // ReLU
    }

    // Lớp output (Sigmoid)
    float z2 = b2[0];
    for (int i = 0; i < ANN_N_HIDDEN; i++) z2 += W2[0][i] * h[i];
    return 1.0f / (1.0f + expf(-z2));   // Sigmoid -> xác suất [0,1]
}
