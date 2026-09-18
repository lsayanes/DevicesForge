#!/usr/bin/env python3
"""
Paso 4 — Medir si el modelo mejora las IR (sin Cubase).

Para cada par en ml/data/synthetic/val:
  1. STFT de la IR ruidosa
  2. Inferencia ONNX → magnitud limpia predicha
  3. ISTFT con fase de la ruidosa
  4. Comparar SNR vs la IR limpia de referencia

Uso:
  python3 scripts/evaluate_ir.py
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import onnxruntime as ort
import soundfile as sf

ML_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ML_ROOT))

from df_stft import (  # noqa: E402
    magnitude_log_to_waveform,
    pad_time_bins,
    snr_db,
    waveform_to_magnitude_log,
)


def run_onnx(session: ort.InferenceSession, mag_log: np.ndarray) -> np.ndarray:
    """mag_log: (F, T) → salida (F, T) (misma T que entrada)."""
    padded, pad_cols = pad_time_bins(mag_log)
    inp = padded[np.newaxis, np.newaxis, :, :].astype(np.float32)
    out = session.run(["magnitude_log_clean"], {"magnitude_log": inp})[0]
    result = out[0, 0]
    if pad_cols:
        result = result[:, :-pad_cols]
    return result


def main() -> None:
    val_dir = ML_ROOT / "data" / "synthetic" / "val"
    onnx_path = ML_ROOT / "models" / "ir_denoise_v1.onnx"

    if not onnx_path.is_file():
        print(f"No existe {onnx_path}. Corré export_onnx.py primero.")
        sys.exit(1)

    print("DevicesForge — evaluar denoise ONNX")
    session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])

    snr_before_list: list[float] = []
    snr_after_list: list[float] = []

    pairs = sorted(val_dir.glob("*_noisy.wav"))
    if not pairs:
        print(f"No hay archivos en {val_dir}")
        sys.exit(1)

    for noisy_path in pairs:
        stem = noisy_path.name.replace("_noisy.wav", "")
        clean_path = val_dir / f"{stem}_clean.wav"
        noisy, _ = sf.read(noisy_path, dtype="float32")
        clean, _ = sf.read(clean_path, dtype="float32")

        mag_noisy, phase = waveform_to_magnitude_log(noisy)
        mag_pred = run_onnx(session, mag_noisy)
        denoised = magnitude_log_to_waveform(mag_pred, phase, length=len(noisy))

        snr_before_list.append(snr_db(clean, noisy))
        snr_after_list.append(snr_db(clean, denoised))

    mean_before = float(np.mean(snr_before_list))
    mean_after = float(np.mean(snr_after_list))
    delta = mean_after - mean_before

    print(f"  pares evaluados: {len(pairs)}")
    print(f"  SNR medio (noisy vs clean):    {mean_before:.2f} dB")
    print(f"  SNR medio (denoised vs clean): {mean_after:.2f} dB")
    print(f"  mejora media:                  {delta:+.2f} dB")

    if delta < 1.0:
        print("\n  Nota: mejora baja — podés entrenar más epochs o revisar datos.")
    else:
        print("\n  OK para seguir con integración C++ en el plugin.")

    report_path = ML_ROOT / "models" / "evaluate_report.txt"
    report_path.write_text(
        f"snr_before_db={mean_before:.4f}\n"
        f"snr_after_db={mean_after:.4f}\n"
        f"snr_delta_db={delta:.4f}\n",
        encoding="utf-8",
    )
    print(f"  reporte: {report_path}")


if __name__ == "__main__":
    main()
