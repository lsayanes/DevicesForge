#!/usr/bin/env python3
"""
Paso 2 — Entrenar una red U-Net pequeña para limpiar magnitud STFT.

Entrada de la red:  magnitud log1p de la IR ruidosa  (canal único)
Salida de la red:   magnitud log1p de la IR limpia   (canal único)

La fase no la predice la red; en inferencia se usa la fase de la IR ruidosa
(igual que hará el plugin en C++).

Lee pares *_clean.wav / *_noisy.wav de ml/data/synthetic/train|val.

Guarda el mejor checkpoint en ml/checkpoints/unet_best.pt

Uso:
  python3 scripts/train_unet_denoise.py
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, Dataset

ML_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ML_ROOT))

from df_stft import N_FREQ, waveform_to_magnitude_log  # noqa: E402
from unet_model import UNetDenoise  # noqa: E402

# Parche temporal en frames STFT (recorte aleatorio si la IR es más larga)
PATCH_FRAMES = 64
BATCH_SIZE = 4
EPOCHS = 8
LEARNING_RATE = 1e-3
DEVICE = "cuda" if torch.cuda.is_available() else "cpu"


class IRPairDataset(Dataset):
    """Carga pares WAV y devuelve trozos (F, T_patch) de magnitud log."""

    def __init__(self, directory: Path, patch_frames: int) -> None:
        self.patch_frames = patch_frames
        self.pairs: list[tuple[Path, Path]] = []
        for noisy_path in sorted(directory.glob("*_noisy.wav")):
            stem = noisy_path.name.replace("_noisy.wav", "")
            clean_path = directory / f"{stem}_clean.wav"
            if clean_path.is_file():
                self.pairs.append((noisy_path, clean_path))
        if not self.pairs:
            raise FileNotFoundError(
                f"No hay pares *_noisy.wav / *_clean.wav en {directory}. "
                "Ejecutá primero generate_synthetic_pairs.py"
            )

    def __len__(self) -> int:
        return len(self.pairs)

    def __getitem__(self, idx: int) -> tuple[torch.Tensor, torch.Tensor]:
        noisy_path, clean_path = self.pairs[idx]
        import soundfile as sf

        noisy, _sr = sf.read(noisy_path, dtype="float32")
        clean, _sr2 = sf.read(clean_path, dtype="float32")
        assert _sr == _sr2

        mag_noisy, _phase = waveform_to_magnitude_log(noisy)
        mag_clean, _phase2 = waveform_to_magnitude_log(clean)

        # Recorte aleatorio en el eje tiempo (dim 1)
        n_frames = mag_noisy.shape[1]
        if n_frames <= self.patch_frames:
            # Pad con ceros si hiciera falta
            pad = self.patch_frames - n_frames
            mag_noisy = np.pad(mag_noisy, ((0, 0), (0, pad)))
            mag_clean = np.pad(mag_clean, ((0, 0), (0, pad)))
            start = 0
        else:
            start = np.random.randint(0, n_frames - self.patch_frames)
            mag_noisy = mag_noisy[:, start : start + self.patch_frames]
            mag_clean = mag_clean[:, start : start + self.patch_frames]

        # Forma PyTorch: (1, F, T)
        x = torch.from_numpy(mag_noisy).unsqueeze(0)
        y = torch.from_numpy(mag_clean).unsqueeze(0)
        return x, y


def main() -> None:
    train_dir = ML_ROOT / "data" / "synthetic" / "train"
    val_dir = ML_ROOT / "data" / "synthetic" / "val"
    ckpt_dir = ML_ROOT / "checkpoints"
    ckpt_dir.mkdir(parents=True, exist_ok=True)

    print("DevicesForge — entrenar U-Net denoise")
    print(f"  device = {DEVICE}")
    print(f"  F (bins freq) = {N_FREQ}")

    train_ds = IRPairDataset(train_dir, PATCH_FRAMES)
    val_ds = IRPairDataset(val_dir, PATCH_FRAMES)
    train_loader = DataLoader(train_ds, batch_size=BATCH_SIZE, shuffle=True)
    val_loader = DataLoader(val_ds, batch_size=BATCH_SIZE, shuffle=False)

    model = UNetDenoise().to(DEVICE)
    optimizer = torch.optim.Adam(model.parameters(), lr=LEARNING_RATE)
    loss_fn = nn.L1Loss()

    best_val = float("inf")
    best_path = ckpt_dir / "unet_best.pt"

    for epoch in range(1, EPOCHS + 1):
        model.train()
        train_loss = 0.0
        for x, y in train_loader:
            x = x.to(DEVICE)
            y = y.to(DEVICE)
            pred = model(x)
            loss = loss_fn(pred, y)
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            train_loss += loss.item() * x.size(0)
        train_loss /= len(train_ds)

        model.eval()
        val_loss = 0.0
        with torch.no_grad():
            for x, y in val_loader:
                x = x.to(DEVICE)
                y = y.to(DEVICE)
                pred = model(x)
                val_loss += loss_fn(pred, y).item() * x.size(0)
        val_loss /= len(val_ds)

        print(
            f"  epoch {epoch:2d}/{EPOCHS}  train_L1={train_loss:.5f}  val_L1={val_loss:.5f}",
            flush=True,
        )

        if val_loss < best_val:
            best_val = val_loss
            torch.save(
                {
                    "model_state": model.state_dict(),
                    "patch_frames": PATCH_FRAMES,
                    "val_l1": val_loss,
                },
                best_path,
            )

    print(f"Mejor checkpoint: {best_path}  (val_L1={best_val:.5f})")
    print("Siguiente paso: python3 scripts/export_onnx.py")


if __name__ == "__main__":
    main()
