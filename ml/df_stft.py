"""
Utilidades STFT para DevicesForge (Fase 3).

Este archivo es la única pieza "técnica" compartida entre scripts.
Los scripts en scripts/ lo importan para no repetir las mismas fórmulas.

Contrato (debe coincidir con docs/AI-PHASE3.md y el futuro C++ del plugin):
  - 48 kHz
  - ventana Hann, n_fft=2048, hop=512
  - magnitud en escala log1p para la red neuronal
"""

from __future__ import annotations

import numpy as np
from scipy.signal import istft, stft

# --- Constantes del contrato v1 ---
SAMPLE_RATE = 48_000
N_FFT = 2048
HOP = 512
N_FREQ = N_FFT // 2 + 1  # 1025 bins


def waveform_to_magnitude_log(wave: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """
    Convierte una IR (1D float) en magnitud log1p del STFT.

    Devuelve:
      magnitude_log: forma (N_FREQ, n_frames)
      phase:         forma (N_FREQ, n_frames) — la fase de la IR ruidosa;
                     en el plugin C++ se reutiliza al reconstruir (ISTFT).
    """
    wave = np.asarray(wave, dtype=np.float64).ravel()
    _f, _t, zxx = stft(
        wave,
        fs=SAMPLE_RATE,
        nperseg=N_FFT,
        noverlap=N_FFT - HOP,
        window="hann",
        boundary=None,
        padded=False,
    )
    magnitude = np.abs(zxx)
    phase = np.angle(zxx)
    magnitude_log = np.log1p(magnitude).astype(np.float32)
    return magnitude_log, phase.astype(np.float32)


def magnitude_log_to_waveform(
    magnitude_log: np.ndarray,
    phase: np.ndarray,
    length: int,
) -> np.ndarray:
    """
    Reconstruye waveform desde magnitud limpia (log) + fase original.
    """
    magnitude = np.expm1(np.maximum(magnitude_log, 0.0))
    zxx = magnitude * np.exp(1j * phase)
    _t, wave = istft(
        zxx,
        fs=SAMPLE_RATE,
        nperseg=N_FFT,
        noverlap=N_FFT - HOP,
        window="hann",
    )
    wave = wave[:length]
    if len(wave) < length:
        wave = np.pad(wave, (0, length - len(wave)))
    return wave.astype(np.float32)


# La U-Net hace 2 poolings en el eje tiempo → T debe ser múltiplo de 4 en inferencia.
UNET_TIME_PAD_MULTIPLE = 4


def pad_time_bins(mag_log: np.ndarray, multiple: int = UNET_TIME_PAD_MULTIPLE) -> tuple[np.ndarray, int]:
    """Rellena columnas al final del STFT para que T sea múltiplo de `multiple`. Devuelve (array, pad)."""
    f_bins, t_bins = mag_log.shape
    assert f_bins == N_FREQ
    pad = (multiple - (t_bins % multiple)) % multiple
    if pad == 0:
        return mag_log, 0
    padded = np.pad(mag_log, ((0, 0), (0, pad)), mode="edge")
    return padded, pad


def snr_db(clean: np.ndarray, estimate: np.ndarray) -> float:
    """Relación señal/ruido en dB (útil para evaluate_ir.py)."""
    clean = clean.astype(np.float64)
    estimate = estimate.astype(np.float64)
    noise = clean - estimate
    power_signal = np.mean(clean**2) + 1e-12
    power_noise = np.mean(noise**2) + 1e-12
    return float(10.0 * np.log10(power_signal / power_noise))
