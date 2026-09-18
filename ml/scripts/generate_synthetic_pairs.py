#!/usr/bin/env python3
"""
Paso 1 del laboratorio IA — Generar pares de IR limpia / ruidosa.

Qué hace (en orden):
  1. Crea IR "limpias" sintéticas (impulsos + colas, filtros suaves).
  2. Les suma ruido (SNR aleatorio) → IR "ruidosa".
  3. Guarda pares .wav en ml/data/synthetic/train y .../val.

No entrena nada. Solo prepara datos para train_unet_denoise.py.

Uso (desde la carpeta ml/):
  python3 scripts/generate_synthetic_pairs.py
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import soundfile as sf

# Permite importar df_stft.py desde ml/
ML_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ML_ROOT))

from df_stft import SAMPLE_RATE  # noqa: E402

# Longitud fija de cada IR en samples (~0.34 s @ 48 kHz; suficiente para entrenar parches)
IR_LENGTH = 8192

NUM_TRAIN = 64
NUM_VAL = 16


def _pink_noise(n: int) -> np.ndarray:
    """Ruido rosa simple (1/f) para simular hiss de interfaz."""
    white = np.random.randn(n)
    # Filtro integrador muy básico en dominio frecuencia
    spec = np.fft.rfft(white)
    freqs = np.fft.rfftfreq(n, d=1.0 / SAMPLE_RATE) + 1e-6
    spec *= 1.0 / np.sqrt(freqs)
    pink = np.fft.irfft(spec, n=n)
    pink /= np.max(np.abs(pink)) + 1e-9
    return pink.astype(np.float32)


def make_clean_ir(length: int) -> np.ndarray:
    """
    Una IR limpia inventada: impulso al inicio + cola amortiguada + tono filtrado.
    No pretende ser un preamp real; enseña a la red a recuperar estructura vs ruido.
    """
    n = length
    ir = np.zeros(n, dtype=np.float32)

    # Impulso principal
    ir[0] = 1.0

    # Cola exponencial
    decay = np.exp(-np.arange(n) / (SAMPLE_RATE * 0.08))
    ir += 0.35 * decay * np.sin(2 * np.pi * 120.0 * np.arange(n) / SAMPLE_RATE)

    # Segundo impulso más débil (reflexión simulada)
    delay = int(SAMPLE_RATE * 0.003)
    if delay < n:
        ir[delay] += 0.2

    # Suavizar con filtro IIR de un polo (tono del "dispositivo")
    alpha = 0.12
    for i in range(1, n):
        ir[i] += alpha * ir[i - 1]

    peak = np.max(np.abs(ir)) + 1e-9
    return (ir / peak).astype(np.float32)


def add_noise(clean: np.ndarray, snr_db: float) -> np.ndarray:
    """Mezcla ruido rosa hasta alcanzar el SNR deseado (en dB)."""
    noise = _pink_noise(len(clean))
    signal_power = np.mean(clean**2) + 1e-12
    noise_power = np.mean(noise**2) + 1e-12
    target_noise_power = signal_power / (10 ** (snr_db / 10.0))
    scale = np.sqrt(target_noise_power / noise_power)
    noisy = clean + noise * scale
    peak = np.max(np.abs(noisy)) + 1e-9
    return (noisy / peak).astype(np.float32)


def write_pair(out_dir: Path, index: int, clean: np.ndarray, noisy: np.ndarray) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    name = f"pair_{index:04d}"
    sf.write(out_dir / f"{name}_clean.wav", clean, SAMPLE_RATE, subtype="FLOAT")
    sf.write(out_dir / f"{name}_noisy.wav", noisy, SAMPLE_RATE, subtype="FLOAT")


def main() -> None:
    rng = np.random.default_rng(42)
    train_dir = ML_ROOT / "data" / "synthetic" / "train"
    val_dir = ML_ROOT / "data" / "synthetic" / "val"

    print("DevicesForge — generar pares sintéticos")
    print(f"  sample_rate = {SAMPLE_RATE} Hz")
    print(f"  ir_length   = {IR_LENGTH} samples")
    print(f"  train/val   = {NUM_TRAIN} / {NUM_VAL}")

    for i in range(NUM_TRAIN):
        clean = make_clean_ir(IR_LENGTH)
        snr = float(rng.uniform(8.0, 32.0))
        noisy = add_noise(clean, snr)
        write_pair(train_dir, i, clean, noisy)

    for i in range(NUM_VAL):
        clean = make_clean_ir(IR_LENGTH)
        snr = float(rng.uniform(8.0, 32.0))
        noisy = add_noise(clean, snr)
        write_pair(val_dir, i, clean, noisy)

    print(f"Listo. WAV en:\n  {train_dir}\n  {val_dir}")
    print("Siguiente paso: python3 scripts/train_unet_denoise.py")


if __name__ == "__main__":
    main()
