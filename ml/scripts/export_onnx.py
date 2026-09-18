#!/usr/bin/env python3
"""
Paso 3 — Exportar el checkpoint PyTorch a ONNX (ir_denoise_v1.onnx).

El plugin C++ cargará este archivo con ONNX Runtime.

Nombres de tensores (contrato v1):
  entrada:  magnitude_log        [1, 1, F, T]  T variable
  salida:   magnitude_log_clean  [1, 1, F, T]

Uso:
  python3 scripts/export_onnx.py
"""

from __future__ import annotations

import sys
from pathlib import Path

import torch

ML_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ML_ROOT))

from df_stft import N_FREQ  # noqa: E402
from unet_model import UNetDenoise  # noqa: E402

ONNX_OPSET = 17
EXAMPLE_T = 128  # solo para el trace de exportación; T puede variar en runtime


def main() -> None:
    ckpt_path = ML_ROOT / "checkpoints" / "unet_best.pt"
    out_path = ML_ROOT / "models" / "ir_denoise_v1.onnx"

    if not ckpt_path.is_file():
        print(f"No existe {ckpt_path}. Corré train_unet_denoise.py primero.")
        sys.exit(1)

    print("DevicesForge — export ONNX")
    checkpoint = torch.load(ckpt_path, map_location="cpu")
    model = UNetDenoise()
    model.load_state_dict(checkpoint["model_state"])
    model.eval()

    dummy = torch.randn(1, 1, N_FREQ, EXAMPLE_T)

    out_path.parent.mkdir(parents=True, exist_ok=True)

    torch.onnx.export(
        model,
        dummy,
        str(out_path),
        input_names=["magnitude_log"],
        output_names=["magnitude_log_clean"],
        dynamic_axes={
            "magnitude_log": {0: "batch", 3: "time_frames"},
            "magnitude_log_clean": {0: "batch", 3: "time_frames"},
        },
        opset_version=ONNX_OPSET,
        do_constant_folding=True,
    )

    print(f"ONNX guardado: {out_path}")
    print(f"  val_L1 del checkpoint = {checkpoint.get('val_l1', 'n/a')}")
    print("Copiá opcionalmente a:")
    print("  ~/Library/Application Support/DevicesForge/models/ir_denoise_v1.onnx")
    print("Siguiente paso: python3 scripts/evaluate_ir.py")


if __name__ == "__main__":
    main()
