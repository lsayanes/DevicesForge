# Fase 3 — Denoise IA local (STFT + overlap)

> **Primera lectura:** si el detalle técnico abruma, empezá por [`AI-PHASE3-GUIA.md`](AI-PHASE3-GUIA.md) (4 scripts lineales en `ml/scripts/`).

Plan de implementación para **limpiar IRs capturadas** con una red neuronal desplegada vía **ONNX Runtime**, sin API cloud en esta fase.

## Decisiones acordadas

| Tema | Decisión |
|------|----------|
| Función IA | **Denoise** — reducir ruido y artefactos de medición **sin** cambiar a propósito el timbre del dispositivo |
| Dominio | **STFT + overlap-add** (IR largas, coherente con “clonar tono”) |
| Cloud | **Pospuesto** hasta que el pipeline local sea estable y evaluable |
| Promediado N capturas | **Fuera del MVP** (mejora clásica opcional en una sub-fase posterior) |
| Entrenamiento | **PyTorch** (u otro) **fuera** del plugin; el plugin solo **inferencia** con `.onnx` |

## Qué es ONNX en este proyecto

```text
  ml/ (Python)                    plugin (C++)
  ─────────────                   ────────────
  dataset → entrenar → export     ONNX Runtime carga ir_denoise.onnx
           ir_denoise.onnx   →   STFT → tensor → inferencia → ISTFT → IR limpia
```

**ONNX no entrena redes.** Define el grafo y los tipos de tensores; **ONNX Runtime** ejecuta el modelo en CPU (y opcionalmente GPU más adelante).

Infraestructura ya presente en el repo:

- [`src/ai/ONNXInference.cpp`](../src/ai/ONNXInference.cpp) — sesión ORT, `denoiseIR()` (hoy asume waveform 1D; hay que adaptar o añadir capa STFT).
- Parámetro VST **AI** (`AI_DENOISE`, id 1004) — leído en el processor, **aún no** aplicado tras la captura.
- Build opcional `HAS_ONNX_RUNTIME` — ver [`docs/DEPENDENCIES.md`](DEPENDENCIES.md).

## Objetivo del MVP

Tras una captura exitosa (`processCompletedCapture`):

1. Deconvolución + ventaneo + normalización (como hoy).
2. Si **AI = On** y el modelo está cargado: **denoise STFT** sobre la IR en memoria.
3. Guardar en `IRManager` y exportar (opcional: también `IR_pre_ai.float.wav` para A/B).
4. Si no hay modelo o falla inferencia: **IR clásica** sin cambios + entrada en `capture_log.txt`.

La inferencia corre **post-captura** (no en cada bloque de `process()`), para no bloquear el audio thread del DAW con IR de decenas de miles de samples.

## Contrato STFT (v1)

Valores iniciales alineados al diseño 48 kHz del plugin; remuestrear o rechazar si el host ≠ 48 kHz hasta soportar multi-rate.

| Parámetro | Valor | Notas |
|-----------|-------|--------|
| `sample_rate` | 48000 | Debe coincoincidir con entrenamiento v1 |
| `n_fft` | 2048 | Resolución freq ≈ 23.4 Hz @ 48 kHz |
| `hop` | 512 | 75 % overlap entre frames STFT |
| Ventana análisis | Hann | Misma en análisis y síntesis (COLA con overlap-add) |
| Bins de freq | `F = n_fft/2 + 1` → **1025** | Magnitud por bin |

**Estrategia de fase (MVP):** la red predice **magnitud limpia**; la **fase** se conserva de la IR de entrada (noisy). Es el compromiso habitual: buen denoise perceptual con menos riesgo de destruir la respuesta en fase del dispositivo.

Representación enviada al modelo (log comprimido, estable numéricamente):

```text
x = log1p(magnitude)     # float32
y = log1p(magnitude_clean)  # target en entrenamiento
```

En inferencia: `magnitude_clean = expm1(y)` con clamp ≥ 0.

## Contrato ONNX (v1)

Nombre de archivo sugerido: `ir_denoise_v1.onnx`.

| Tensor | Nombre | Shape | Dtype |
|--------|--------|-------|-------|
| Entrada | `magnitude_log` | `[1, 1, F, T]` | float32 |
| Salida | `magnitude_log_clean` | `[1, 1, F, T]` | float32 |

- **F** = 1025 fijo.
- **T** = número de frames STFT a lo largo de la IR → eje **dinámico** en ONNX (`T` variable por longitud de IR).

Arquitectura recomendada en entrenamiento: **U-Net 2D** sobre el mapa tiempo–frecuencia (profundidad moderada, ~1–5 M parámetros para CPU en tiempo razonable post-captura).

Nombres de I/O deben coincidir con [`ONNXInference`](../src/ai/ONNXInference.cpp) tras la refactor (sustituir los placeholders `"input"` / `"output"` actuales).

**Padding en inferencia:** la U-Net hace dos poolings en el eje tiempo; antes de llamar a ONNX, rellenar `T` hasta un **múltiplo de 4** (función `pad_time_bins` en [`ml/df_stft.py`](../ml/df_stft.py)). Recortar la salida al `T` original.

### IR largas: troceo en tiempo + overlap

La IR puede tener **~60k+ samples**. El espectrograma completo tiene `T ≈ ceil((N - n_fft) / hop) + 1`.

1. Calcular STFT completo de la IR (o por segmentos contiguos de **T_chunk = 256** frames con **T_overlap = 64** frames entre chunks en el eje tiempo del espectrograma).
2. Por cada chunk: inferencia ONNX → magnitud limpia del chunk.
3. **Crossfade** en la zona solapada (ventana Hann en el eje T del espectrograma).
4. Reconstruir con **ISTFT + overlap-add** a waveform.

Documentar constantes en [`DevicesForge.h`](../src/plugin/DevicesForge.h) cuando se implemente (`AI_STFT_NFFT`, `AI_STFT_HOP`, etc.).

## Ubicación del modelo

Orden de búsqueda al activar el plugin:

1. `~/Library/Application Support/DevicesForge/models/ir_denoise_v1.onnx` (macOS)
2. `%APPDATA%/DevicesForge/models/ir_denoise_v1.onnx` (Windows, cuando aplique)
3. (Opcional) recurso embebido en el bundle del `.vst3`

No versionar el `.onnx` pesado en git; sí versionar **script de export** y checksum en `ml/models/README.md`.

## Pipeline ML (`ml/`)

Estructura propuesta:

```text
ml/
  README.md                 # requisitos Python, comandos rápidos
  requirements.txt          # torch, onnx, onnxruntime, numpy, scipy, soundfile
  data/
    clean/                  # IR limpias (WAV float, 48 kHz)
    synthetic/              # pares generados on-the-fly (no commitear volumen)
  scripts/
    generate_synthetic_pairs.py
    train_unet_denoise.py
    export_onnx.py
    evaluate_ir.py          # SNR, L1 en magnitud, opcional conv-test
  models/
    README.md               # versión, fecha, métricas, opset ONNX
```

### Datos de entrenamiento (v1)

**Clean:**

- IR de loopback `--loopback` del host (identidad ≈ impulso limpio).
- IR sintéticas: exponenciales amortiguadas, filtros IIR suaves, impulsos + colas modeladas.
- (Más adelante) capturas reales de consola/preamp con buena S/N.

**Noisy (degradación sintética):**

- Ruido rosa / blanco a SNR 10–40 dB.
- Hiss filtrado (high-shelf).
- THD suave (tanh con ganancia baja).
- Pequeño retardo/jitter de muestra (opcional).
- Residual de deconvolución (mezclar cola aleatoria).

Cada ejemplo: misma longitud temporal o pad/truncate a **N_max** (p. ej. 65536 samples) con ventana de cola.

### Entrenamiento

- Loss: **L1** en `magnitude_log_clean` (+ opcional peso extra en bins 20 Hz–20 kHz).
- Optimizer Adam, early stopping en val set sintético.
- Validación: SNR waveform, error espectral medio, escucha A/B en convolución con tono de prueba.

### Export

- Opset ONNX ≥ 17 (compatible con ORT 1.24 del repo).
- Verificar con `onnxruntime` Python y con `DevicesForgeHost` / test C++ antes de integrar en Cubase.

## Pipeline plugin (C++)

Nuevos módulos sugeridos:

| Módulo | Responsabilidad |
|--------|-----------------|
| `STFTProcessor` (o reutilizar FFT existente) | STFT/ISTFT Hann, overlap-add |
| `IRDenoiseONNX` | Troceo T, llamadas a `ONNXInference`, ensamblado |
| Cambios en `PluginProcessor::processCompletedCapture` | Rama AI tras `IRPostProcessor` |
| `setActive` | `aiEngine.initialize(sr)` + `loadModel(path)` |

Estado en `capture_log.txt` (campos nuevos):

```text
ai_requested=1
ai_model_loaded=1
ai_applied=1
ai_inference_ms=42
```

Si `ai_requested=1` pero `ai_applied=0`, incluir `ai_reason=no_model|inference_failed|sample_rate_unsupported`.

## Sub-fases e hitos

| Id | Entregable | Criterio de done |
|----|------------|------------------|
| **3.0** | Este documento + plan Cursor | Contrato STFT/ONNX acordado |
| **3.1** | `ml/` scripts + primer `ir_denoise_v1.onnx` | Mejora SNR ≥ 3 dB en set sintético val |
| **3.2** | `STFTProcessor` + `IRDenoiseONNX` + tests unitarios | WAV ruidoso → WAV limpio offline |
| **3.3** | Cableado post-captura + param **AI** | Cubase: AI On mejora IR ruidosa vs Off |
| **3.4** | Export A/B + doc usuario | `IR.wav` vs `IR_ai.wav` en `latest/` |

**Fuera de alcance Fase 3 MVP:**

- API cloud.
- Style transfer / extrapolación de cola.
- Denoise en tiempo real en `process()`.
- Promediado automático de N capturas.

## Relación con el roadmap (`SPEC.md`)

Fase 3 checklist actualizado:

- [ ] Entrenar y exportar modelo de denoising (STFT U-Net → ONNX)
- [ ] Integrar ONNX Runtime en post-captura (STFT + overlap)
- [ ] ~~API cloud fallback~~ → **Fase 3+ / posterior**
- [ ] ~~Optimización genérica de IR~~ → acotado a **denoise** en MVP; extrapolación después

## Referencias

- [`docs/CAPTURE-RING-BUFFER.md`](CAPTURE-RING-BUFFER.md) — captura y Monitor en Cubase
- [`docs/WINDOWING-NORMALIZATION.md`](WINDOWING-NORMALIZATION.md) — ventaneo previo al denoise
- [`docs/EXPORT-FORMATS.md`](EXPORT-FORMATS.md) — salida WAV/AIFF
- [ONNX Runtime](https://onnxruntime.ai)
- [PyTorch ONNX export](https://pytorch.org/docs/stable/onnx.html)

---

*Documento v1.0 — Fase 3 IA — Septiembre 2026*
