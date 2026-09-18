"""Definición de la red U-Net (compartida entre entrenamiento y export ONNX)."""

from __future__ import annotations

import torch
import torch.nn as nn


class ConvBlock(nn.Module):
    def __init__(self, in_ch: int, out_ch: int) -> None:
        super().__init__()
        self.net = nn.Sequential(
            nn.Conv2d(in_ch, out_ch, kernel_size=3, padding=1),
            nn.ReLU(inplace=True),
            nn.Conv2d(out_ch, out_ch, kernel_size=3, padding=1),
            nn.ReLU(inplace=True),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(x)


def _crop_to_match(tensor: torch.Tensor, reference: torch.Tensor) -> torch.Tensor:
    _, _, h, w = reference.shape
    return tensor[:, :, :h, :w]


class UNetDenoise(nn.Module):
    """Entrada/salida: (batch, 1, F, T) magnitud log1p."""

    def __init__(self) -> None:
        super().__init__()
        # Pool solo en tiempo (eje W). F=1025 no es múltiplo de 2 en frecuencia.
        self.enc1 = ConvBlock(1, 16)
        self.pool1 = nn.MaxPool2d(kernel_size=(1, 2), stride=(1, 2))
        self.enc2 = ConvBlock(16, 32)
        self.pool2 = nn.MaxPool2d(kernel_size=(1, 2), stride=(1, 2))
        self.bottleneck = ConvBlock(32, 64)
        self.up2 = nn.ConvTranspose2d(64, 32, kernel_size=(1, 2), stride=(1, 2))
        self.dec2 = ConvBlock(64, 32)
        self.up1 = nn.ConvTranspose2d(32, 16, kernel_size=(1, 2), stride=(1, 2))
        self.dec1 = ConvBlock(32, 16)
        self.out = nn.Conv2d(16, 1, kernel_size=1)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        e1 = self.enc1(x)
        e2 = self.enc2(self.pool1(e1))
        b = self.bottleneck(self.pool2(e2))
        d2 = self.up2(b)
        d2 = _crop_to_match(d2, e2)
        d2 = self.dec2(torch.cat([d2, e2], dim=1))
        d1 = self.up1(d2)
        d1 = _crop_to_match(d1, e1)
        d1 = self.dec1(torch.cat([d1, e1], dim=1))
        return self.out(d1)
