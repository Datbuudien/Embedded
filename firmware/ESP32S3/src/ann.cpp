/**
 * ann.cpp — ANN Inference Engine (Edge AI, bare-metal)
 * Kiến trúc: Single-layer linear model + sigmoid activation
 * Mục đích: Binary classification tưới/không tưới
 * Target: FPU hardware LX7 (ESP32-S3), < 1ms/cycle
 */

#include "ann.h"
#include "config.h"
#include "protocol.h"
#include <math.h>
#include <Arduino.h>

// ---- Private State ----------------------------------------
static float s_weights[5];
static float s_bias;
static float s_last_prob = 0.0f;

// ---- Implementation ----------------------------------------

void ann_init() {
    s_weights[0] = ANN_W1;   // soil_pct
    s_weights[1] = ANN_W2;   // temperature
    s_weights[2] = ANN_W3;   // humidity_air
    s_weights[3] = ANN_W4;   // current_hour
    s_weights[4] = ANN_W5;   // rain_norm
    s_bias       = ANN_BIAS;
    Serial.println("[ANN] Weights loaded from config.h");
}

uint8_t ann_infer(float soil_pct, float temp, float hum,
                  uint8_t hour, float rain_norm, bool rain_digital) {

    // =========================================================
    //  HARD EXCEPTIONS — Thực thi TRƯỚC sigmoid, không override
    // =========================================================

    // KI-002: Thermal Shock — cấm tưới khi T > 38°C
    // Cơ sở sinh lý: khí khổng đóng (stomatal closure), tưới nước
    // lạnh gây chênh lệch nhiệt cục bộ → hoại tử rễ cây
    if (temp > TEMP_MAX_WATER) {
        Serial.printf("[ANN] HARD BLOCK: Nhiệt độ %.1f°C > 38°C — Cấm tưới (Thermal Shock)\n", temp);
        s_last_prob = 0.0f;
        return CMD_OFF;
    }

    // KI-009: Cảm biến mưa DO active-LOW — true = đang mưa
    // Cấm tưới khi đang mưa, không có exception nào override được
    if (rain_digital) {
        Serial.println("[ANN] HARD BLOCK: Đang mưa (rain_digital=true) — Cấm tưới");
        s_last_prob = 0.0f;
        return CMD_OFF;
    }

    // =========================================================
    //  ANN INFERENCE — Linear combination + sigmoid
    //  Z = w1*x1 + w2*x2 + w3*x3 + w4*x4 + w5*x5 + b
    //  P = sigmoid(Z) = 1 / (1 + e^(-Z))
    // =========================================================
    float Z = s_weights[0] * soil_pct
            + s_weights[1] * temp
            + s_weights[2] * hum
            + s_weights[3] * (float)hour
            + s_weights[4] * rain_norm
            + s_bias;

    // Sigmoid — FPU hardware tính expf() nhanh (< 1ms)
    float P = 1.0f / (1.0f + expf(-Z));
    s_last_prob = P;

    Serial.printf("[ANN] Z=%.3f P=%.3f → %s\n", Z, P,
                  P > ANN_THRESHOLD ? "CMD_ON (Tưới)" : "CMD_OFF (Không tưới)");

    return (P > ANN_THRESHOLD) ? CMD_ON : CMD_OFF;
}

float ann_get_last_prob() {
    return s_last_prob;
}
