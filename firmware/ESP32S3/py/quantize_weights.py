"""
=============================================================
  quantize_weights.py
  Lượng tử hóa trọng số float32 -> INT8
  Chạy sau khi có model_meta.json từ train_irrigation_model.py

  CHẠY:
    python quantize_weights.py

  ĐẦU RA:
    weights_q8.h  –  header C chứa trọng số int8 + hàm inference
=============================================================
"""

import numpy as np
import json
import os

# ─────────────────────────────────────────────────────────────
#  Tải lại trọng số từ file weights_export.h (parse thủ công)
#  hoặc lưu thêm weights.npz khi train (cách khuyên dùng)
# ─────────────────────────────────────────────────────────────

def load_weights_from_npz(path="weights.npz"):
    """
    Cách 1 (khuyên dùng): thêm vào cuối main() của train_irrigation_model.py:
        np.savez("weights.npz", W1=model.W1, b1=model.b1, W2=model.W2, b2=model.b2)
    """
    data = np.load(path)
    return data["W1"], data["b1"], data["W2"], data["b2"]


# ─────────────────────────────────────────────────────────────
#  Hàm lượng tử hóa đối xứng (Symmetric INT8)
# ─────────────────────────────────────────────────────────────

def quantize_symmetric(arr: np.ndarray):
    """
    Lượng tử hóa đối xứng: zero_point = 0
        scale = max(|arr|) / 127.0
        q     = clip(round(arr / scale), -127, 127)

    Đơn giản hơn asymmetric, phù hợp cho trọng số (phân phối gần chuẩn).
    """
    abs_max = np.abs(arr).max()
    if abs_max < 1e-9:
        return np.zeros_like(arr, dtype=np.int8), 1.0, 0

    scale      = abs_max / 127.0
    q          = np.clip(np.round(arr / scale), -127, 127).astype(np.int8)
    zero_point = 0
    return q, scale, zero_point


def quantize_asymmetric(arr: np.ndarray):
    """
    Lượng tử hóa bất đối xứng: full INT8 range [-128, 127]
        scale      = (max - min) / 255.0
        zero_point = round(-min / scale) - 128
        q          = clip(round(arr / scale) + zero_point, -128, 127)

    Tốt hơn cho activation (bias thường lệch về một phía).
    """
    v_min, v_max = arr.min(), arr.max()
    if v_max - v_min < 1e-9:
        return np.zeros_like(arr, dtype=np.int8), 1.0, 0

    scale      = (v_max - v_min) / 255.0
    zero_point = int(np.round(-v_min / scale)) - 128
    zero_point = int(np.clip(zero_point, -128, 127))
    q          = np.clip(np.round(arr / scale).astype(int) + zero_point,
                         -128, 127).astype(np.int8)
    return q, scale, zero_point


def dequantize(q: np.ndarray, scale: float, zero_point: int) -> np.ndarray:
    return scale * (q.astype(float) - zero_point)


# ─────────────────────────────────────────────────────────────
#  Đánh giá sai số lượng tử hóa
# ─────────────────────────────────────────────────────────────

def evaluate_quantization_error(original: np.ndarray,
                                 q: np.ndarray,
                                 scale: float,
                                 zero_point: int,
                                 name: str):
    reconstructed = dequantize(q, scale, zero_point)
    mae  = np.abs(original - reconstructed).mean()
    mse  = ((original - reconstructed) ** 2).mean()
    psnr = 10 * np.log10(np.max(original**2) / mse) if mse > 0 else float('inf')
    print(f"  {name:6s}: MAE={mae:.6f}  MSE={mse:.8f}  PSNR={psnr:.1f} dB")
    return reconstructed


# ─────────────────────────────────────────────────────────────
#  Xuất file C header INT8
# ─────────────────────────────────────────────────────────────

def export_q8_header(W1, b1, W2, b2, scaler_min, scaler_max, path="weights_q8.h"):
    """
    Tạo file weights_q8.h chứa trọng số int8 và hàm inference INT8.
    """
    # Lượng tử hóa
    W1_q, sc_W1, zp_W1 = quantize_symmetric(W1)
    b1_q, sc_b1, zp_b1 = quantize_asymmetric(b1)
    W2_q, sc_W2, zp_W2 = quantize_symmetric(W2)
    b2_q, sc_b2, zp_b2 = quantize_asymmetric(b2)

    nh, ni = W1.shape
    no, _  = W2.shape

    print("\n[Q8 Sai số lượng tử hóa]")
    evaluate_quantization_error(W1, W1_q, sc_W1, zp_W1, "W1")
    evaluate_quantization_error(b1, b1_q, sc_b1, zp_b1, "b1")
    evaluate_quantization_error(W2, W2_q, sc_W2, zp_W2, "W2")
    evaluate_quantization_error(b2, b2_q, sc_b2, zp_b2, "b2")

    def arr_to_c_int8(arr, name, dims):
        lines = [f"static const int8_t {name}{dims} = {{"]
        flat  = arr.flatten().tolist()
        row   = []
        for i, v in enumerate(flat):
            row.append(f"{int(v)}")
            if (i + 1) % 16 == 0 or i == len(flat) - 1:
                lines.append("  " + ", ".join(row) + ",")
                row = []
        lines.append("};")
        return "\n".join(lines)

    header = f"""// ================================================================
//  weights_q8.h  –  Trọng số INT8 (Quantized)
//  Auto-generated bởi quantize_weights.py
//  Kiến trúc: Input({ni}) -> Hidden({nh}, ReLU) -> Output({no}, Sigmoid)
//
//  Phương pháp: Symmetric INT8 cho W, Asymmetric INT8 cho bias
// ================================================================
#pragma once
#include <stdint.h>
#include <math.h>

// ── Kích thước ───────────────────────────────────────────────
#define Q8_N_INPUT   {ni}
#define Q8_N_HIDDEN  {nh}
#define Q8_N_OUTPUT  {no}
#define Q8_THRESHOLD 0.50f

// ── Chuẩn hóa đầu vào (dùng chung với float32) ──────────────
static const float Q8_SCALE_MIN[{ni}] = {{ {', '.join(f'{v:.4f}f' for v in scaler_min)} }};
static const float Q8_SCALE_MAX[{ni}] = {{ {', '.join(f'{v:.4f}f' for v in scaler_max)} }};

// ── Tham số lượng tử hóa ─────────────────────────────────────
#define Q8_SCALE_W1  {sc_W1:.8f}f
#define Q8_ZP_W1     {zp_W1}
#define Q8_SCALE_W2  {sc_W2:.8f}f
#define Q8_ZP_W2     {zp_W2}
#define Q8_SCALE_B1  {sc_b1:.8f}f
#define Q8_ZP_B1     {zp_b1}
#define Q8_SCALE_B2  {sc_b2:.8f}f
#define Q8_ZP_B2     {zp_b2}

// ── Trọng số INT8 ─────────────────────────────────────────────
{arr_to_c_int8(W1_q, 'Q8_W1', f'[{nh}][{ni}]')}

static const int8_t Q8_b1[{nh}] = {{ {', '.join(str(int(v)) for v in b1_q.flatten())} }};

{arr_to_c_int8(W2_q, 'Q8_W2', f'[{no}][{nh}]')}

static const int8_t Q8_b2[{no}] = {{ {', '.join(str(int(v)) for v in b2_q.flatten())} }};

// ================================================================
//  HÀM SUY LUẬN INT8 – nhanh hơn float32 ~3x trên ESP32-S3
// ================================================================
static inline float _q8_norm(float val, int idx) {{
    float range = Q8_SCALE_MAX[idx] - Q8_SCALE_MIN[idx];
    if (range < 1e-6f) return 0.0f;
    float n = (val - Q8_SCALE_MIN[idx]) / range;
    return (n < 0.f) ? 0.f : (n > 1.f ? 1.f : n);
}}

float ANN_infer_q8(float do_am_dat, float nhiet_do,
                   float do_am_kk,  float gio) {{
    // Chuẩn hóa (vẫn dùng float, nhẹ hơn inference)
    float x[Q8_N_INPUT] = {{
        _q8_norm(do_am_dat, 0),
        _q8_norm(nhiet_do,  1),
        _q8_norm(do_am_kk,  2),
        _q8_norm(gio,       3),
    }};

    // Lượng tử hóa đầu vào -> int16 (tránh overflow khi cộng dồn)
    int16_t xq[Q8_N_INPUT];
    for (int j = 0; j < Q8_N_INPUT; j++)
        xq[j] = (int16_t)(x[j] * 127);

    // Lớp ẩn: dot product int8 x int8 -> accumulate int32, dequant, ReLU
    float h[Q8_N_HIDDEN];
    for (int i = 0; i < Q8_N_HIDDEN; i++) {{
        int32_t acc = (int32_t)(Q8_b1[i] - Q8_ZP_B1) * 16384; // scale bias
        for (int j = 0; j < Q8_N_INPUT; j++)
            acc += (int32_t)(Q8_W1[i][j] - Q8_ZP_W1) * xq[j];
        // De-quantize và ReLU
        float z = (float)acc * (Q8_SCALE_W1 / 127.0f) + Q8_SCALE_B1 * (Q8_b1[i] - Q8_ZP_B1);
        h[i] = (z > 0.0f) ? z : 0.0f;
    }}

    // Lớp output (float để đảm bảo độ chính xác sigmoid)
    float z2 = Q8_SCALE_B2 * (Q8_b2[0] - Q8_ZP_B2);
    for (int i = 0; i < Q8_N_HIDDEN; i++)
        z2 += Q8_SCALE_W2 * (Q8_W2[0][i] - Q8_ZP_W2) * h[i];

    return 1.0f / (1.0f + expf(-z2));
}}
"""

    with open(path, "w", encoding="utf-8") as f:
        f.write(header)
    print(f"\n  [EXPORT] INT8 header: {os.path.abspath(path)}")


# ─────────────────────────────────────────────────────────────
#  So sánh float32 vs INT8
# ─────────────────────────────────────────────────────────────

def compare_inference(W1, b1, W2, b2, scaler_min, scaler_max):
    """
    Chạy inference float32 vs INT8 trên tập test, so sánh sai số.
    """
    def sigmoid(z):    return 1.0 / (1.0 + np.exp(-z))
    def relu(z):       return np.maximum(0, z)
    def normalize(x, mn, mx): return np.clip((x - mn) / (mx - mn + 1e-9), 0, 1)

    test_cases = [
        # dat   nhiet  amkk   gio
        [35.0,  33.0,  55.0,  14.0],
        [70.0,  25.0,  80.0,   7.0],
        [20.0,  28.0,  60.0,  20.0],
        [50.0,  30.0,  70.0,  13.0],
    ]
    labels = ["dat kho+nong (TUOI?)", "dat uot (KHONG?)",
              "ban dem (KHONG?)", "binh thuong (?)"]

    W1_q, sc_W1, zp_W1 = quantize_symmetric(W1)
    b1_q, sc_b1, zp_b1 = quantize_asymmetric(b1)
    W2_q, sc_W2, zp_W2 = quantize_symmetric(W2)
    b2_q, sc_b2, zp_b2 = quantize_asymmetric(b2)

    print("\n[So sánh Float32 vs INT8]")
    print(f"{'Case':<25} {'Float32':>10} {'INT8':>10} {'Sai so':>10}")
    print("─" * 58)

    for tc, lbl in zip(test_cases, labels):
        x = np.array([normalize(tc[0], scaler_min[0], scaler_max[0]),
                      normalize(tc[1], scaler_min[1], scaler_max[1]),
                      normalize(tc[2], scaler_min[2], scaler_max[2]),
                      normalize(tc[3], scaler_min[3], scaler_max[3])])

        # Float32
        h_f  = relu(W1 @ x + b1.flatten())
        p_f  = float(sigmoid(W2 @ h_f + b2.flatten())[0])

        # INT8 (approx)
        W1_dq = dequantize(W1_q, sc_W1, zp_W1)
        b1_dq = dequantize(b1_q, sc_b1, zp_b1).flatten()
        W2_dq = dequantize(W2_q, sc_W2, zp_W2)
        b2_dq = dequantize(b2_q, sc_b2, zp_b2).flatten()
        h_q  = relu(W1_dq @ x + b1_dq)
        p_q  = float(sigmoid(W2_dq @ h_q + b2_dq)[0])

        err  = abs(p_f - p_q)
        print(f"  {lbl:<23} {p_f:>10.4f} {p_q:>10.4f} {err:>10.4f}")


# ─────────────────────────────────────────────────────────────
#  MAIN
# ─────────────────────────────────────────────────────────────
def main():
    print("\n" + "="*55)
    print("  QUANTIZATION PIPELINE – INT8 cho ESP32-S3")
    print("="*55)

    # Tải trọng số
    if os.path.exists("weights.npz"):
        W1, b1, W2, b2 = load_weights_from_npz("weights.npz")
        print("\n  [LOAD] Trọng số từ weights.npz")
    else:
        print("\n  [WARN] Không tìm thấy weights.npz")
        print("         Thêm dòng sau vào cuối main() của train_irrigation_model.py:")
        print("         np.savez('weights.npz', W1=model.W1, b1=model.b1, W2=model.W2, b2=model.b2)")
        print("\n         Đang tạo trọng số giả để demo...")
        rng = np.random.default_rng(42)
        W1 = rng.normal(0, 0.5, (8, 4))
        b1 = rng.normal(0, 0.1, (8, 1))
        W2 = rng.normal(0, 0.5, (1, 8))
        b2 = rng.normal(0, 0.1, (1, 1))

    # Tải metadata scaler
    if os.path.exists("model_meta.json"):
        with open("model_meta.json") as f:
            meta = json.load(f)
        scaler_min = meta["scaler_min"]
        scaler_max = meta["scaler_max"]
    else:
        # Giá trị mặc định hợp lý
        scaler_min = [10.0, 20.0, 40.0, 0.0]
        scaler_max = [90.0, 40.0, 90.0, 24.0]

    print(f"\n  W1: {W1.shape}  W2: {W2.shape}")

    # So sánh float32 vs INT8
    compare_inference(W1, b1, W2, b2, scaler_min, scaler_max)

    # Export header INT8
    export_q8_header(W1, b1, W2, b2, scaler_min, scaler_max)

    print(f"\n{'='*55}")
    print("  HOÀN TẤT. Copy file weights_q8.h vào project Arduino.")
    print(f"{'='*55}\n")


if __name__ == "__main__":
    main()
