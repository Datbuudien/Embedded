"""
=============================================================
  train_irrigation_model.py
  Pipeline huấn luyện ANN cho hệ thống tưới tiêu thông minh
  Đầu ra: file weights_export.h để nhúng vào ESP32-S3
=============================================================

CÀI ĐẶT:
  pip install numpy pandas scikit-learn matplotlib

LUỒNG SỬ DỤNG:
  1. Chuẩn bị file CSV (xem ĐỊNH DẠNG bên dưới)
  2. Chạy: python train_irrigation_model.py
  3. Copy file weights_export.h vào project Arduino của S3
=============================================================
"""

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import MinMaxScaler
from sklearn.metrics import (accuracy_score, confusion_matrix,
                             classification_report, roc_auc_score)
import json
import os

# ─────────────────────────────────────────────────────────────
#  1. CẤU HÌNH KIẾN TRÚC & SIÊU THAM SỐ
# ─────────────────────────────────────────────────────────────
CFG = {
    # Kiến trúc mạng
    "n_input":    4,        # do_am_dat, nhiet_do, do_am_kk, gio
    "n_hidden":   8,        # số nơ-ron lớp ẩn (tăng nếu có nhiều dữ liệu)
    "n_output":   1,        # xác suất cần tưới

    # Huấn luyện
    "learning_rate": 0.05,
    "epochs":        3000,
    "batch_size":    16,    # None = full-batch gradient descent
    "lambda_l2":     1e-4,  # hệ số regularization L2

    # Dữ liệu
    "test_size":    0.2,
    "random_seed":  42,

    # Ngưỡng quyết định
    "threshold":    0.5,
}

# ─────────────────────────────────────────────────────────────
#  2. TẠO DỮ LIỆU MẪU (thay bằng CSV thực tế của bạn)
# ─────────────────────────────────────────────────────────────
"""
ĐỊNH DẠNG CSV THỰC TẾ:
  do_am_dat,nhiet_do,do_am_kk,gio,can_tuoi
  25.3,34.5,55.0,14,1
  70.1,28.0,72.0,8,0
  ...

  can_tuoi:
    1 = cần tưới (đất khô, nóng, ít ẩm)
    0 = không cần (đất đủ ướt hoặc ban đêm)
"""

def generate_synthetic_data(n_samples: int = 1200) -> pd.DataFrame:
    """
    Tạo dữ liệu tổng hợp theo các quy tắc nông học:
    - Đất khô (<35%) + nhiệt cao (>32°C) + ban ngày => cần tưới
    - Đất ướt (>65%) => không cần dù điều kiện khác
    - Ban đêm (22h-5h)  => không tưới
    """
    rng = np.random.default_rng(CFG["random_seed"])

    do_am_dat = rng.uniform(10, 90, n_samples)
    nhiet_do  = rng.uniform(20, 40, n_samples)
    do_am_kk  = rng.uniform(40, 90, n_samples)
    gio        = rng.uniform(0,  24, n_samples)

    # Luật nhãn (domain knowledge)
    ban_ngay   = ((gio >= 5) & (gio <= 21)).astype(float)
    dat_kho    = (do_am_dat < 40).astype(float)
    nhiet_cao  = (nhiet_do  > 30).astype(float)
    dat_uot    = (do_am_dat > 65).astype(float)

    score = (dat_kho * 0.5 + nhiet_cao * 0.3 + (1 - do_am_kk/100) * 0.2) * ban_ngay
    can_tuoi = ((score > 0.35) & (dat_uot == 0)).astype(int)

    # Thêm nhiễu 5%
    noise_mask = rng.random(n_samples) < 0.05
    can_tuoi[noise_mask] = 1 - can_tuoi[noise_mask]

    return pd.DataFrame({
        "do_am_dat": do_am_dat.round(1),
        "nhiet_do":  nhiet_do.round(1),
        "do_am_kk":  do_am_kk.round(1),
        "gio":        gio.round(1),
        "can_tuoi":   can_tuoi,
    })


# ─────────────────────────────────────────────────────────────
#  3. LỚP MẠNG NƠ-RON (numpy thuần, không framework)
# ─────────────────────────────────────────────────────────────
class IrrigationANN:
    """
    Mạng nơ-ron 1 lớp ẩn, phân loại nhị phân.
    Kiến trúc: Input(4) -> Hidden(n_hidden, ReLU) -> Output(1, Sigmoid)
    """

    def __init__(self, cfg: dict):
        self.cfg = cfg
        rng = np.random.default_rng(cfg["random_seed"])
        ni, nh, no = cfg["n_input"], cfg["n_hidden"], cfg["n_output"]

        # Khởi tạo trọng số He initialization (tốt cho ReLU)
        self.W1 = rng.normal(0, np.sqrt(2.0 / ni), (nh, ni))
        self.b1 = np.zeros((nh, 1))
        self.W2 = rng.normal(0, np.sqrt(2.0 / nh), (no, nh))
        self.b2 = np.zeros((no, 1))

        # Cache cho backprop
        self._cache = {}
        self.history = {"loss": [], "val_loss": [], "acc": [], "val_acc": []}

    # ── Hàm kích hoạt ──────────────────────────────────────
    @staticmethod
    def relu(z):     return np.maximum(0, z)
    @staticmethod
    def relu_d(z):   return (z > 0).astype(float)
    @staticmethod
    def sigmoid(z):  return 1.0 / (1.0 + np.exp(-np.clip(z, -500, 500)))

    # ── Forward Pass ───────────────────────────────────────
    def forward(self, X: np.ndarray) -> np.ndarray:
        """X shape: (n_input, m)"""
        Z1 = self.W1 @ X + self.b1        # (nh, m)
        A1 = self.relu(Z1)                 # (nh, m)
        Z2 = self.W2 @ A1 + self.b2       # (no, m)
        A2 = self.sigmoid(Z2)              # (no, m)

        self._cache = {"X": X, "Z1": Z1, "A1": A1, "Z2": Z2, "A2": A2}
        return A2

    # ── Hàm mất mát (BCE + L2) ─────────────────────────────
    def loss(self, Y: np.ndarray, A2: np.ndarray) -> float:
        m = Y.shape[1]
        eps = 1e-9
        bce = -(Y * np.log(A2 + eps) + (1 - Y) * np.log(1 - A2 + eps)).mean()
        l2  = (self.cfg["lambda_l2"] / (2 * m)) * (
              np.sum(self.W1**2) + np.sum(self.W2**2))
        return float(bce + l2)

    # ── Backward Pass ──────────────────────────────────────
    def backward(self, Y: np.ndarray) -> dict:
        m  = Y.shape[1]
        A2 = self._cache["A2"]
        A1 = self._cache["A1"]
        Z1 = self._cache["Z1"]
        X  = self._cache["X"]
        lam = self.cfg["lambda_l2"]

        dZ2 = A2 - Y                                             # (no, m)
        dW2 = (dZ2 @ A1.T) / m + (lam / m) * self.W2
        db2 = dZ2.mean(axis=1, keepdims=True)

        dA1 = self.W2.T @ dZ2                                   # (nh, m)
        dZ1 = dA1 * self.relu_d(Z1)
        dW1 = (dZ1 @ X.T) / m  + (lam / m) * self.W1
        db1 = dZ1.mean(axis=1, keepdims=True)

        return {"dW1": dW1, "db1": db1, "dW2": dW2, "db2": db2}

    # ── Cập nhật trọng số ──────────────────────────────────
    def update(self, grads: dict):
        lr = self.cfg["learning_rate"]
        self.W1 -= lr * grads["dW1"]
        self.b1 -= lr * grads["db1"]
        self.W2 -= lr * grads["dW2"]
        self.b2 -= lr * grads["db2"]

    # ── Dự đoán ────────────────────────────────────────────
    def predict_proba(self, X: np.ndarray) -> np.ndarray:
        return self.forward(X).flatten()

    def predict(self, X: np.ndarray) -> np.ndarray:
        return (self.predict_proba(X) >= self.cfg["threshold"]).astype(int)

    # ── Huấn luyện ─────────────────────────────────────────
    def fit(self, X_tr, Y_tr, X_val, Y_val):
        m = X_tr.shape[1]
        bs = self.cfg["batch_size"] or m
        thr = self.cfg["threshold"]

        print(f"\n{'='*55}")
        print(f"  Huấn luyện: {self.cfg['epochs']} epochs  |  lr={self.cfg['learning_rate']}")
        print(f"  Kiến trúc: {self.cfg['n_input']}→{self.cfg['n_hidden']}→{self.cfg['n_output']}")
        print(f"  Tập train: {m} mẫu  |  Tập val: {X_val.shape[1]} mẫu")
        print(f"{'='*55}")

        for ep in range(1, self.cfg["epochs"] + 1):
            # Xáo trộn mini-batch
            idx = np.random.permutation(m)
            for start in range(0, m, bs):
                batch = idx[start:start + bs]
                Xb = X_tr[:, batch]
                Yb = Y_tr[:, batch]
                A2 = self.forward(Xb)
                self.update(self.backward(Yb))

            # Ghi lại loss & accuracy
            if ep % 10 == 0 or ep == 1:
                A_tr = self.forward(X_tr)
                A_va = self.forward(X_val)
                ltr = self.loss(Y_tr, A_tr)
                lva = self.loss(Y_val, A_va)
                acc_tr = accuracy_score(Y_tr.flatten(),
                                        (A_tr.flatten() >= thr).astype(int))
                acc_va = accuracy_score(Y_val.flatten(),
                                        (A_va.flatten() >= thr).astype(int))
                self.history["loss"].append(ltr)
                self.history["val_loss"].append(lva)
                self.history["acc"].append(acc_tr)
                self.history["val_acc"].append(acc_va)

                if ep % 200 == 0:
                    print(f"  Epoch {ep:4d}  loss={ltr:.4f}  val_loss={lva:.4f}"
                          f"  acc={acc_tr:.3f}  val_acc={acc_va:.3f}")

        print(f"\n  [Hoàn tất] val_acc = {self.history['val_acc'][-1]:.4f}")
        return self


# ─────────────────────────────────────────────────────────────
#  4. XUẤT TRỌNG SỐ RA FILE C HEADER
# ─────────────────────────────────────────────────────────────
def export_weights_header(model: IrrigationANN,
                          scaler: MinMaxScaler,
                          path: str = "weights_export.h"):
    """
    Tạo file .h chứa trọng số + tham số chuẩn hóa.
    Paste vào project Arduino của ESP32-S3.
    """
    W1 = model.W1   # (nh, ni)
    b1 = model.b1   # (nh, 1)
    W2 = model.W2   # (no, nh)
    b2 = model.b2   # (no, 1)

    nh, ni = W1.shape
    no, _  = W2.shape

    def arr_to_c(arr, name, dims):
        lines = [f"static const float {name}{dims} = {{"]
        flat  = arr.flatten().tolist()
        row   = []
        for i, v in enumerate(flat):
            row.append(f"{v:.8f}f")
            if (i + 1) % 8 == 0 or i == len(flat) - 1:
                lines.append("  " + ", ".join(row) + ",")
                row = []
        lines.append("};")
        return "\n".join(lines)

    scale_min = scaler.data_min_.tolist()
    scale_max = scaler.data_max_.tolist()

    header = f"""// ================================================================
//  weights_export.h  –  Auto-generated bởi train_irrigation_model.py
//  KHÔNG chỉnh sửa thủ công.
//  Kiến trúc: Input({ni}) -> Hidden({nh}, ReLU) -> Output({no}, Sigmoid)
// ================================================================
#pragma once
#include <math.h>

// ── Kích thước ───────────────────────────────────────────────
#define ANN_N_INPUT   {ni}
#define ANN_N_HIDDEN  {nh}
#define ANN_N_OUTPUT  {no}
#define ANN_THRESHOLD {model.cfg['threshold']:.2f}f

// ── Tham số chuẩn hóa MinMax (từ tập train) ─────────────────
//    X_norm = (X - X_min) / (X_max - X_min)
static const float SCALE_MIN[{ni}] = {{ {', '.join(f'{v:.4f}f' for v in scale_min)} }};
static const float SCALE_MAX[{ni}] = {{ {', '.join(f'{v:.4f}f' for v in scale_max)} }};

// ── Trọng số lớp ẩn W1[{nh}][{ni}] ──────────────────────────
{arr_to_c(W1, 'W1', f'[{nh}][{ni}]')}

// ── Bias lớp ẩn b1[{nh}] ────────────────────────────────────
static const float b1[{nh}] = {{ {', '.join(f'{v:.8f}f' for v in b1.flatten())} }};

// ── Trọng số lớp output W2[{no}][{nh}] ──────────────────────
{arr_to_c(W2, 'W2', f'[{no}][{nh}]')}

// ── Bias lớp output b2[{no}] ─────────────────────────────────
static const float b2[{no}] = {{ {', '.join(f'{v:.8f}f' for v in b2.flatten())} }};

// ================================================================
//  HÀM SUY LUẬN (inline, gọi từ module AI trong Arduino)
// ================================================================
inline float ann_normalize(float val, int idx) {{
    float range = SCALE_MAX[idx] - SCALE_MIN[idx];
    if (range < 1e-6f) return 0.0f;
    float norm = (val - SCALE_MIN[idx]) / range;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    return norm;
}}

inline float ann_run(float do_am_dat, float nhiet_do,
                     float do_am_kk,  float gio) {{
    // Đầu vào thô -> chuẩn hóa
    float x[ANN_N_INPUT] = {{
        ann_normalize(do_am_dat, 0),
        ann_normalize(nhiet_do,  1),
        ann_normalize(do_am_kk,  2),
        ann_normalize(gio,       3),
    }};

    // Lớp ẩn (ReLU)
    float h[ANN_N_HIDDEN];
    for (int i = 0; i < ANN_N_HIDDEN; i++) {{
        float z = b1[i];
        for (int j = 0; j < ANN_N_INPUT; j++) z += W1[i][j] * x[j];
        h[i] = (z > 0.0f) ? z : 0.0f;   // ReLU
    }}

    // Lớp output (Sigmoid)
    float z2 = b2[0];
    for (int i = 0; i < ANN_N_HIDDEN; i++) z2 += W2[0][i] * h[i];
    return 1.0f / (1.0f + expf(-z2));   // Sigmoid -> xác suất [0,1]
}}
"""

    with open(path, "w", encoding="utf-8") as f:
        f.write(header)
    print(f"\n  [EXPORT] Trọng số đã lưu: {os.path.abspath(path)}")


# ─────────────────────────────────────────────────────────────
#  5. VISUALIZE KẾT QUẢ
# ─────────────────────────────────────────────────────────────
def plot_results(model: IrrigationANN, X_val, Y_val):
    fig, axes = plt.subplots(1, 3, figsize=(15, 4))
    fig.suptitle("Kết quả Huấn luyện ANN – Hệ thống Tưới tiêu", fontsize=13)

    ep_range = range(10, CFG["epochs"] + 1, 10)

    # ── Loss curve ──
    ax = axes[0]
    ax.plot(ep_range, model.history["loss"],     label="Train Loss",      color="#3B8BD4")
    ax.plot(ep_range, model.history["val_loss"], label="Val Loss",  ls="--", color="#E85D24")
    ax.set_title("Loss theo Epoch"); ax.set_xlabel("Epoch"); ax.legend()
    ax.grid(True, alpha=0.3)

    # ── Accuracy curve ──
    ax = axes[1]
    ax.plot(ep_range, model.history["acc"],     label="Train Acc",       color="#3B8BD4")
    ax.plot(ep_range, model.history["val_acc"], label="Val Acc",   ls="--", color="#E85D24")
    ax.set_title("Accuracy theo Epoch"); ax.set_xlabel("Epoch"); ax.legend()
    ax.set_ylim(0, 1); ax.grid(True, alpha=0.3)

    # ── Confusion Matrix ──
    ax = axes[2]
    Y_pred = model.predict(X_val)
    cm = confusion_matrix(Y_val.flatten(), Y_pred)
    im = ax.imshow(cm, cmap="Blues")
    ax.set_xticks([0, 1]); ax.set_yticks([0, 1])
    ax.set_xticklabels(["Không tưới", "Tưới"])
    ax.set_yticklabels(["Không tưới", "Tưới"])
    ax.set_xlabel("Dự đoán"); ax.set_ylabel("Thực tế")
    ax.set_title("Ma trận nhầm lẫn")
    for i in range(2):
        for j in range(2):
            ax.text(j, i, str(cm[i, j]), ha="center", va="center",
                    color="white" if cm[i, j] > cm.max() / 2 else "black",
                    fontsize=14, fontweight="bold")
    plt.colorbar(im, ax=ax)

    plt.tight_layout()
    plt.savefig("training_results.png", dpi=120, bbox_inches="tight")
    plt.show()
    print("  [PLOT] Biểu đồ đã lưu: training_results.png")


# ─────────────────────────────────────────────────────────────
#  6. MAIN PIPELINE
# ─────────────────────────────────────────────────────────────
def main():
    print("\n" + "="*55)
    print("  ANN TRAINING PIPELINE – Hệ thống Tưới tiêu Thông minh")
    print("="*55)

    # ── 6.1 Tải hoặc tạo dữ liệu ──
    CSV_PATH = "irrigation_data.csv"
    if os.path.exists(CSV_PATH):
        df = pd.read_csv(CSV_PATH)
        print(f"\n  [DATA] Tải từ file: {CSV_PATH}  ({len(df)} mẫu)")
    else:
        df = generate_synthetic_data(1200)
        df.to_csv(CSV_PATH, index=False)
        print(f"\n  [DATA] Tạo dữ liệu tổng hợp: {len(df)} mẫu -> {CSV_PATH}")

    print(f"  Phân phối nhãn: Tưới={df['can_tuoi'].sum()}  "
          f"Không={len(df)-df['can_tuoi'].sum()}")

    # ── 6.2 Tiền xử lý ──
    features = ["do_am_dat", "nhiet_do", "do_am_kk", "gio"]
    X_raw = df[features].values.astype(float)
    Y_raw = df["can_tuoi"].values.astype(float).reshape(-1, 1)

    scaler = MinMaxScaler()
    X_norm = scaler.fit_transform(X_raw)

    X_tr_r, X_va_r, Y_tr_r, Y_va_r = train_test_split(
        X_norm, Y_raw,
        test_size=CFG["test_size"],
        random_state=CFG["random_seed"],
        stratify=Y_raw
    )

    # Chuyển sang dạng (features, samples)
    X_tr = X_tr_r.T;  Y_tr = Y_tr_r.T
    X_va = X_va_r.T;  Y_va = Y_va_r.T

    # ── 6.3 Huấn luyện ──
    model = IrrigationANN(CFG)
    model.fit(X_tr, Y_tr, X_va, Y_va)

    # ── 6.4 Đánh giá ──
    Y_pred_proba = model.predict_proba(X_va)
    Y_pred       = model.predict(X_va)
    Y_true        = Y_va.flatten().astype(int)

    print(f"\n{'─'*55}")
    print("  ĐÁNH GIÁ TRÊN TẬP KIỂM TRA:")
    print(f"  Accuracy  : {accuracy_score(Y_true, Y_pred):.4f}")
    print(f"  ROC-AUC   : {roc_auc_score(Y_true, Y_pred_proba):.4f}")
    print(f"\n{classification_report(Y_true, Y_pred, target_names=['Không tưới','Tưới'])}")

    # ── 6.5 Lưu trọng số ──
    export_weights_header(model, scaler, "weights_export.h")

    # ── 6.6 Lưu metadata ──
    meta = {
        "architecture": f"Input({CFG['n_input']}) -> Hidden({CFG['n_hidden']}, ReLU) -> Output(1, Sigmoid)",
        "threshold":    CFG["threshold"],
        "val_accuracy": float(accuracy_score(Y_true, Y_pred)),
        "roc_auc":      float(roc_auc_score(Y_true, Y_pred_proba)),
        "scaler_min":   scaler.data_min_.tolist(),
        "scaler_max":   scaler.data_max_.tolist(),
        "features":     features,
    }
    with open("model_meta.json", "w") as f:
        json.dump(meta, f, indent=2, ensure_ascii=False)
    print("  [META] Thông tin mô hình: model_meta.json")

    # ── 6.7 Vẽ biểu đồ ──
    plot_results(model, X_va, Y_va)

    print(f"\n{'='*55}")
    print("  HOÀN TẤT. Copy file weights_export.h vào project Arduino.")
    print(f"{'='*55}\n")


if __name__ == "__main__":
    main()
