/**
 * ann.cpp — ANN Inference Engine (Edge AI, bare-metal)
 *
 * Kien truc: Input(4) -> Hidden(8, ReLU) -> Output(1, Sigmoid)
 * Trong so: INT8 quantized tu weights_q8.h
 *           (~3x nhanh hon float32 vi dung integer multiply)
 * Target: FPU hardware LX7 (ESP32-S3), < 1ms/cycle
 *
 * Phase 2: Nang cap tu single-layer (config.h) len multi-layer (weights_q8.h)
 */

#include "ann.h"
#include "config.h"
#include "protocol.h"
#include "weights_q8.h"   // INT8 quantized weights — auto-generated
#include <math.h>
#include <Arduino.h>

// ---- Private State ----------------------------------------
static float s_last_prob = 0.0f;

// ---- Implementation ----------------------------------------

void ann_init() {
    Serial.println("[ANN] Khoi tao — kien truc INT8 Quantized");
    Serial.printf("[ANN] Input(%d) -> Hidden(%d, ReLU) -> Output(%d, Sigmoid)\n",
                  Q8_N_INPUT, Q8_N_HIDDEN, Q8_N_OUTPUT);
    Serial.printf("[ANN] Nguong quyet dinh: P > %.2f -> CMD_ON\n", Q8_THRESHOLD);
}

uint8_t ann_infer(float soil_pct, float temp, float hum,
                  uint8_t hour, bool rain_digital) {

    // =========================================================
    //  HARD EXCEPTIONS — Thuc thi TRUOC sigmoid, khong override
    // =========================================================

    // KI-002: Thermal Shock — cam tuoi khi T > 38 C
    // Co so sinh ly: khi khong dong (stomatal closure), tuoi nuoc
    // lanh gay chenh lech nhiet cuc bo → hoai tu re cay
    if (temp > TEMP_MAX_WATER) {
        Serial.printf("[ANN] HARD BLOCK: Nhiet do %.1f C > 38 C — Cam tuoi (Thermal Shock)\n", temp);
        s_last_prob = 0.0f;
        return CMD_OFF;
    }

    // KI-009: Cam bien mua DO active-LOW — true = dang mua
    if (rain_digital) {
        Serial.println("[ANN] HARD BLOCK: Dang mua (rain_digital=true) — Cam tuoi");
        s_last_prob = 0.0f;
        return CMD_OFF;
    }

    // =========================================================
    //  ANN INFERENCE — INT8 Quantized Multi-layer
    //  Input(4) -> Hidden(8, ReLU) -> Output(1, Sigmoid)
    // =========================================================

    // Buoc 1: Lay 4 inputs (dua vao model: soil, temp, hum, hour)
    float inputs[Q8_N_INPUT] = {
        soil_pct,
        temp,
        hum,
        (float)hour
    };

    // Buoc 2: Chay inference INT8 qua ham trong weights_q8.h
    float prob = ANN_infer_q8(inputs[0], inputs[1], inputs[2], inputs[3]);
    s_last_prob = prob;

    Serial.printf("[ANN] P=%.3f -> %s\n", prob,
                  prob > Q8_THRESHOLD ? "CMD_ON (Tuoi)" : "CMD_OFF (Khong tuoi)");

    return (prob > Q8_THRESHOLD) ? CMD_ON : CMD_OFF;
}

float ann_get_last_prob() {
    return s_last_prob;
}
