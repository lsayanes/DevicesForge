# Ventaneo (windowing) y normalización de IR

Este documento describe los pasos **3 y 4** del pipeline de procesado definido en [`SPEC.md`](../SPEC.md) (§4.3), después de la captura y la deconvolución FFT.

## Dónde encaja en DevicesForge

```text
Generate → Capture (ring buffer) → Deconvolución → Windowing → Normalización → IR en memoria
```

| Etapa | Módulo | Qué hace |
|-------|--------|----------|
| Captura | `RingCaptureBuffer` | Guarda la respuesta del dispositivo (pre/post trigger) |
| Deconvolución | `SweepDeconvolver` | Extrae la IR cruda: `IR ≈ IFFT( FFT(grabado) / FFT(referencia) )` |
| Limitado en banda | `SweepDeconvolver` | Regulariza la división y descarta lo que está fuera del sweep |
| Ventaneo | `IRPostProcessor` | Atenúa suavemente la cola de la IR para evitar truncamiento abrupto |
| Normalización | `IRPostProcessor` | Escala la IR para un pico objetivo (p. ej. 0 dBFS) |

La referencia del sweep es la misma señal que emitió `SignalGenerator` (`getReference()`), necesaria para dividir en frecuencia.

## Ventaneo (windowing)

La IR cruda suele ser **más larga** de lo útil: al final hay ruido, reverberación de sala o artefactos numéricos. Si cortás la IR de golpe, la convolución introduce **clicks** o **pre-eco**.

Se multiplica la IR en el **tiempo** por una curva que es **1 al inicio** (no se toca el ataque / el impulso) y **0 al final** (fade-out en la cola).

**Importante:** una ventana Hanning *completa* pone `w[0] = 0` y **borra el impulso** de un loopback. El plugin **no** usa eso sobre la IR exportada.

### Fade de cola (por defecto en `process()`)

Último `IR_TAIL_FADE_FRACTION` (25 %) de samples: medio-Hanning de 1 → 0. El comienzo de la IR queda intacto (`applyTailFade`).

### Hanning completo (`applyHanning`)

Ventana clásica, suave, **simétrica** (también anula el primer sample). Queda disponible para otros usos, no es el default del pipeline de captura:

\[
w[n] = 0.5 \left(1 - \cos\left(\frac{2\pi n}{N-1}\right)\right), \quad n = 0 \ldots N-1
\]

### Kaiser

Ventana con parámetro **β** (beta): más control del lobe principal vs. lobe secundario. Útil si hace falta un corte más agresivo en la cola. Beta por defecto en constantes: `IR_KAISER_BETA_DEFAULT`.

### No confundir con

- El **fade in/out del sweep** en `SignalGenerator` (evita clicks al **generar** la excitación).
- El **pre-trigger** del ring buffer (audio **antes** del disparo de captura).

## Normalización

Ajusta la **escala** de la IR para que distintas capturas sean comparables y el convolver no sature ni quede inaudible.

**Peak normalization** (implementada):

1. Buscar `peak = max |x[n]|`.
2. Si `peak > 0`, multiplicar toda la IR por `targetPeak / peak` (por defecto `targetPeak = 1.0`).

Constantes compartidas en [`DevicesForge.h`](../src/plugin/DevicesForge.h): `kPi` / `kPiF`, `kTwoPi` / `kTwoPiF`, `IR_NORMALIZE_PEAK_TARGET`, etc.

Alternativas futuras (no implementadas): normalización por energía/RMS, objetivo en dBFS distinto de 0.

## Regularización y limitado en banda de la deconvolución

Dividir por `FFT(referencia)` solo tiene sentido donde el sweep **tiene energía**. Fuera de su banda (`SWEEP_FREQ_START_HZ` = 20 Hz a `SWEEP_FREQ_END_HZ` = 20 kHz) el denominador es prácticamente cero y la división amplifica ruido de medición hasta varios órdenes de magnitud.

`SweepDeconvolver::deconvolve` aplica dos protecciones:

1. **Regularización de Tikhonov relativa**: `denom = |Ref|² + DECONV_REGULARIZATION · max|Ref|²`. El epsilon escala con la energía real de la referencia, en vez de ser una constante fija.
2. **Peso de banda** (`bandWeight`): 1 dentro de la banda del sweep, 0 fuera, con transición coseno (media octava abajo, 15 % arriba) para no introducir ringing.

`deconvolve` recibe `sampleRate` porque sin él no puede traducir bins a Hz.

> **Por qué importa.** Sin esto, una IR capturada a 48 kHz concentraba el **100 % de su energía entre 20 y 24 kHz** y la banda audible quedaba 44 dB por debajo. Como la normalización por pico escala respecto de esa basura ultrasónica, la IR audible terminaba con un pico de 0,0057: al convolucionar, la emulación era inaudible y con Mix al 100 % (todo wet) no se escuchaba nada. El test `SweepDeconvolverTest.EnergyStaysInSweepBand` bloquea esta regresión.

## Cuándo se ejecuta en el plugin

Al pasar la captura a estado **Complete** (`RingCaptureBuffer::isComplete()`), el processor:

1. Toma `getCapturedMono()` y la referencia del generador.
2. Ejecuta `SweepDeconvolver::deconvolve`.
3. Aplica fade de cola + peak norm vía `IRPostProcessor::process`.
4. Si el pico de `capture_raw` es menor que `CAPTURE_MIN_PEAK`, **no** genera IR (evita ruido normalizado de un buffer vacío).
4. Guarda la IR en `DynamicConvolver` → `IRManager` (nivel 0).

Tras procesar, la IR se exporta automáticamente (ver [`EXPORT-FORMATS.md`](EXPORT-FORMATS.md)).

## Verificación

- Tests unitarios: `IRPostProcessorTest`, `SweepDeconvolverTest` en [`tests/test_DSP.cpp`](../tests/test_DSP.cpp).
- En estudio: tras Generate + retorno cableado, la IR en memoria debería actualizarse (la convolución en `process()` sigue pendiente de cableado completo).

## Referencias

- Farina, A. (2000) — deconvolución por sweep.
- [`CAPTURE-RING-BUFFER.md`](CAPTURE-RING-BUFFER.md) — captura previa al procesado.
