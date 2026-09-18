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
| Ventaneo | `IRPostProcessor` | Atenúa suavemente la cola de la IR para evitar truncamiento abrupto |
| Normalización | `IRPostProcessor` | Escala la IR para un pico objetivo (p. ej. 0 dBFS) |

La referencia del sweep es la misma señal que emitió `SignalGenerator` (`getReference()`), necesaria para dividir en frecuencia.

## Ventaneo (windowing)

La IR cruda suele ser **más larga** de lo útil: al final hay ruido, reverberación de sala o artefactos numéricos. Si cortás la IR de golpe, la convolución introduce **clicks** o **pre-eco**.

Se multiplica la IR en el **tiempo** por una curva que suele ser **1 al inicio** y **0 al final** (fade-out en la cola).

### Hanning (por defecto)

Ventana clásica, suave:

\[
w[n] = 0.5 \left(1 - \cos\left(\frac{2\pi n}{N-1}\right)\right), \quad n = 0 \ldots N-1
\]

En el código se aplica **sobre toda la longitud** de la IR (`IRPostProcessor::applyHanning`).

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

## Cuándo se ejecuta en el plugin

Al pasar la captura a estado **Complete** (`RingCaptureBuffer::isComplete()`), el processor:

1. Toma `getCapturedMono()` y la referencia del generador.
2. Ejecuta `SweepDeconvolver::deconvolve`.
3. Aplica Hanning + peak norm vía `IRPostProcessor::process`.
4. Guarda la IR en `DynamicConvolver` → `IRManager` (nivel 0).

Todavía **no** hay export WAV ni parámetros VST para elegir ventana; eso puede añadirse en Fase 2.4 / editor.

## Verificación

- Tests unitarios: `IRPostProcessorTest`, `SweepDeconvolverTest` en [`tests/test_DSP.cpp`](../tests/test_DSP.cpp).
- En estudio: tras Generate + retorno cableado, la IR en memoria debería actualizarse (la convolución en `process()` sigue pendiente de cableado completo).

## Referencias

- Farina, A. (2000) — deconvolución por sweep.
- [`CAPTURE-RING-BUFFER.md`](CAPTURE-RING-BUFFER.md) — captura previa al procesado.
