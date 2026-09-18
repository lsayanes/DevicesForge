# FFTProcessor — Guía para No-Matemáticos

> **Objetivo:** Entender qué hace `FFTProcessor`, por qué existe, y cómo se usa en DevicesForge, sin necesidad de ser experto en DSP.

---

## Tabla de Contenidos

1. [Qué es la FFT (explicación intuitiva)](#1-qué-es-la-fft)
2. [Conceptos Clave](#2-conceptos-clave)
3. [Cómo Funciona FFTProcessor](#3-cómo-funciona-fftprocessor)
4. [Casos de Uso en el Proyecto](#4-casos-de-uso-en-el-proyecto)
5. [Referencia de la API](#5-referencia-de-la-api)
6. [Glosario](#6-glosario)

---

## 1. Qué es la FFT

### La analogía del prisma

Imagina un prismas de cristal. Le lanzas luz blanca (una señal compleja) y el prisma la descompone en colores individuales (frecuencias).

La FFT hace exactamente lo mismo con el sonido:

```
Señal de audio (tiempo)          FFT              Frecuencias
                                            
  ╱╲  ╱╲  ╱╲    ──────────▶    ║█║             20 Hz:  fuerte
  ╱  ╲╱  ╲╱  ╲                 ║█║             100 Hz: media  
                               ║█║█║            1000 Hz: fuerte
                               ║█║█║            5000 Hz: débil
                               ═══════
                               Frecuencia →
```

- **Entrada:** Una forma de onda que cambia con el tiempo (samples de audio).
- **Salida:** Una lista de frecuencias y cuánta energía tiene cada una.

### Por qué importa en audio

Trabajar en el dominio del tiempo es lento para ciertas operaciones. Por ejemplo, **convolucionar** una señal con una respuesta al impulso (IR) de 4096 samples requeriría ~16 millones de operaciones por bloque. En el dominio de la frecuencia, se reduce a ~4096 multiplicaciones.

```
Tiempo:     O(N²)  ← lento
Frecuencia: O(N)   ← rápido (FFT + multiply + IFFT)
```

### La FFT inversa (IFFT)

Si la FFT descompone sonido en frecuencias, la **IFFT** (Inverse FFT) las vuelve a juntar en una forma de onda. Son operaciones reversibles:

```
Audio ──FFT──▶ Espectro ──modificar──▶ Espectro modificado ──IFFT──▶ Audio modificado
```

---

## 2. Conceptos Clave

### 2.1 Bins de frecuencia

Un **bin** es un "contenedor" que representa un rango de frecuencias. La FFT de `N` samples produce `N/2 + 1` bins.

| fftSize | Bins | Resolución por bin (a 48 kHz) |
|---------|------|-------------------------------|
| 1024 | 513 | 46.9 Hz |
| 2048 | 1025 | 23.4 Hz |
| 4096 | 2049 | 11.7 Hz |
| 8192 | 4097 | 5.9 Hz |

**Cada bin** almacena un número complejo que tiene:
- **Magnitud:** Qué tan fuerte es esa frecuencia.
- **Fase:** Dónde está posicionada la onda en el tiempo.

### 2.2 Números complejos

Un número complejo tiene dos partes: `a + bi`

```
En el código:   std::complex<float>
En la memoria:  [real, imaginario, real, imaginario, ...]  (interleaved)

Para obtener la magnitud:  |z| = sqrt(real² + imag²)
Para obtener la fase:      θ  = atan2(imag, real)
```

**¿Por qué aparecen?** Porque la FFT produce tanto magnitud como fase. Si solo te importa la magnitud (espectro de potencia), puedes ignorar la fase, pero para convolución necesitas ambas.

### 2.3 Potencia de 2

La FFT (algoritmo radix-2) solo funciona con tamaños que son potencia de 2: 256, 512, 1024, 2048, 4096...

**¿Por qué?** Porque el algoritmo divide la señal recursivamente a la mitad. Si `N=1024`, se divide en 2 × 512, luego 4 × 256, etc. Si `N=1000`, esa división no funciona limpiamente.

```cpp
// La validación en FFTProcessor.cpp:
if (size <= 0 || (size & (size - 1)) != 0)
    return false;
// (size & (size-1)) == 0  ← truco de bits que solo es true para potencias de 2
```

### 2.4 Por qué N/2 + 1 bins

Para una señal **real** (como audio), la segunda mitad del espectro es simétrica (espejo) de la primera. Solo necesitamos la primera mitad + el bin central (Nyquist).

```
Bins completos:    [0] [1] [2] ... [N/2-1] [N/2] [N/2+1] ... [N-1]
                   ▲                           ▲
                   DC                          Nyquist
                   (0 Hz)                      (SampleRate/2)

Bins útiles:       [0] [1] [2] ... [N/2-1] [N/2]
                   ←── N/2 + 1 bins ──→
```

Esto ahorra memoria y computación sin perder información.

### 2.5 Escalado 1/N en la IFFT

La FFT "acumula" energía, y la IFFT "divide". Pero pffft (la librería que usamos) **no** aplica la división automáticamente. Por eso `FFTProcessor::inverse()` la hace manualmente:

```cpp
// FFTProcessor.cpp:93-96
float scale = 1.0f / static_cast<float>(fftSize);
for (int32_t i = 0; i < samplesToProcess; i++)
    timeOut[i] *= scale;
```

Sin esto, la señal de salida sería `N` veces más fuerte que la entrada.

---

## 3. Cómo Funciona FFTProcessor

### 3.1 Ciclo de vida

```
┌──────────────┐
│  Constructor │  (no hace nada)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│   prepare()  │  Valida tamaño, crea setup pffft, allocates work area
└──────┬───────┘
       │
       ▼
┌──────────────┐
│  forward()   │  Time domain → Frequency domain
│  inverse()   │  Frequency domain → Time domain
│  complexMul()│  Multiplicación espectral (para convolución)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│    reset()   │  Libera memoria
└──────────────┘
```

### 3.2 prepare()

```cpp
bool FFTProcessor::prepare(int32_t size)
```

1. Libera recursos previos (`reset()`).
2. Valida que `size` sea potencia de 2.
3. Crea el "setup" de pffft (una estructura precomputada que acelera las transforms).
4. Reserva un buffer de trabajo alineado a 16/32 bytes (requerido por SIMD).

```
prepare(4096)
  │
  ├─ Valida: 4096 & 4095 == 0  ✓
  │
  ├─ pffft_new_setup(4096, PFFFT_REAL)
  │   └─ Crea tablas de seno/coseno precomputadas
  │
  └─ pffft_aligned_malloc(4096 * sizeof(float))
      └─ Buffer de 16KB alineado a 32 bytes (para AVX/NEON)
```

### 3.3 forward() — Tiempo → Frecuencia

```cpp
void FFTProcessor::forward(const float* timeIn,
                           std::complex<float>* freqOut,
                           int32_t numSamples)
```

```
Entrada:  [0.1, 0.3, -0.2, 0.5, ...]  (N samples float)
                        │
                        ▼
            ┌─────────────────────┐
            │  Copiar a buffer   │  pffft requiere memoria alineada
            │  alineado + zero-  │  Si numSamples < fftSize, el resto
            │  padding           │  se rellena con ceros
            └─────────┬──────────┘
                      │
                      ▼
            ┌─────────────────────┐
            │  pffft_transform_   │  La FFT real de pffft produce
            │  ordered(FORWARD)   │  N/2+1 complejos (interleaved)
            └─────────┬──────────┘
                      │
                      ▼
Salida:   [(0.5+0.1i), (0.3-0.2i), ...]  (N/2+1 complex<float>)
```

**Detalle importante:** `forward()` hace `malloc` + `free` en cada llamada. Esto es una optimización pendiente — en la práctica, `DynamicConvolver` pre-alloca los buffers espectrales y solo llama a `forward()` con datos ya preparados.

### 3.4 inverse() — Frecuencia → Tiempo

```cpp
void FFTProcessor::inverse(const std::complex<float>* freqIn,
                           float* timeOut,
                           int32_t numSamples)
```

```
Entrada:  [(0.5+0.1i), (0.3-0.2i), ...]  (N/2+1 complex<float>)
                        │
                        ▼
            ┌─────────────────────┐
            │  pffft_transform_   │  Transformada inversa
            │  ordered(BACKWARD)  │  (no normaliza)
            └─────────┬──────────┘
                      │
                      ▼
            ┌─────────────────────┐
            │  Escalar × 1/N      │  Normalización manual
            └─────────┬──────────┘
                      │
                      ▼
Salida:   [0.1, 0.3, -0.2, 0.5, ...]  (N samples float)
```

### 3.5 complexMultiply() — Multiplicación espectral

```cpp
void FFTProcessor::complexMultiply(const std::complex<float>* a,
                                   const std::complex<float>* b,
                                   std::complex<float>* result,
                                   int32_t numBins)
```

Esto es **la clave de la convolución eficiente**. Multiplicación de complejos bin a bin:

```
a[i] = (ar + ai·i)
b[i] = (br + bi·i)

a[i] × b[i] = (ar·br - ai·bi) + (ar·bi + ai·br)·i
```

En código, `std::complex<float>` hace esto automáticamente:

```cpp
result[i] = a[i] * b[i];  // C++ hace la multiplicación de complejos
```

**¿Por qué funciona para convolución?** Porque la convolución en el tiempo equivale a la multiplicación en la frecuencia:

```
Convolución:     y[n] = Σ x[k] · h[n-k]    ← O(N²) operaciones
Frecuencia:      Y = X · H                    ← O(N) operaciones
                                              (después de FFT/IFFT)
```

---

## 4. Casos de Uso en el Proyecto

### 4.1 DynamicConvolver — Convolución en Tiempo Real

El `DynamicConvolver` usa **overlap-add** para convolucionar audio en tiempo real con una IR:

```
                    FFTProcessor
                         │
    ┌────────────────────┼────────────────────┐
    │                    │                    │
    ▼                    ▼                    ▼
forward(ir)         forward(input)       inverse(output)
    │                    │                    │
    ▼                    ▼                    ▼
irSpectrum         inputSpectrum         outputBlock
    │                    │                    │
    └────────┬───────────┘                    │
             │                                │
             ▼                                │
       complexMultiply()                      │
             │                                │
             ▼                                │
       outputSpectrum ───────────────────────▶│
                                              │
                                              ▼
                                      overlapBuffer + outputBlock
                                              │
                                              ▼
                                        Audio de salida
```

**Pasos en código** (`DynamicConvolver::processChannel()`):

```cpp
// 1. FFT de la IR (una sola vez por proceso)
fftProcessor->forward(ir, irSpectrum, irLength);

// 2. Para cada bloque de audio:
for (int32_t offset = 0; offset < numSamples; offset += fftSize) {
    // 2a. Zero-pad + FFT del bloque de entrada
    fftProcessor->forward(paddedInput, inputSpectrum, fftSize);

    // 2b. Multiplicar espectros (convolución en frecuencia)
    fftProcessor->complexMultiply(irSpectrum, inputSpectrum, outputSpectrum, numBins);

    // 2c. IFFT para volver al tiempo
    fftProcessor->inverse(outputSpectrum, outputBlock, fftSize);

    // 2d. Overlap-add: sumar con el buffer de solapamiento
    for (i = 0; i < fftSize; i++)
        data[offset + i] = data[offset + i] * (1 - mix) +
                           (outputBlock[i] + overlapBuffer[i]) * mix;
}
```

### 4.2 SweepDeconvolver — Obtener la IR de un Sweep

El sweep deconvolution usa FFT para dividir la señal grabada entre la señal de referencia:

```
Señal grabada (sweep through device)
         │
         ▼
    FFT(recording) ──────┐
                         │
                         ▼
                   División espectral
                   IR = FFT(rec) / FFT(ref)
                         │
    FFT(reference) ──────┘
         │
         ▼
    IFFT(IR spectrum) → Impulse Response
```

**El problema:** No se puede dividir directamente (división por cero cuando `ref` es silencio). Se usa **regularización de Wiener**:

```cpp
// SweepDeconvolver.cpp:107-113
constexpr float kEpsilon = 1e-8f;
for (int32_t bin = 0; bin < numBins; ++bin) {
    const complex<float>& den = refSpec[bin];
    const float denom = std::norm(den) + kEpsilon;  // |ref|² + epsilon
    irSpec[bin] = (recSpec[bin] * std::conj(den)) / denom;
}
```

**¿Qué hace esto?** En lugar de `rec/ref`, calcula `rec × conj(ref) / (|ref|² + ε)`. Cuando `|ref|` es grande, el resultado es cercano a `rec/ref`. Cuando es pequeño (silencio), `ε` evita la explosión numérica.

---

## 5. Referencia de la API

### Métodos

| Método | Entrada | Salida | Descripción |
|--------|---------|--------|-------------|
| `prepare(size)` | `int32_t` (potencia de 2) | `bool` | Inicializa el FFTProcessor |
| `reset()` | — | — | Libera toda la memoria |
| `forward(timeIn, freqOut, numSamples)` | `float*` (N samples) | `complex<float>*` (N/2+1 bins) | FFT real → espectro |
| `inverse(freqIn, timeOut, numSamples)` | `complex<float>*` (N/2+1 bins) | `float*` (N samples) | Espectro → IFFT con 1/N |
| `complexMultiply(a, b, result, numBins)` | 2 espectros + numBins | `complex<float>*` | Multiplicación bin a bin |
| `getFFTSize()` | — | `int32_t` | Tamaño FFT configurado |
| `getNumBins()` | — | `int32_t` | `fftSize / 2 + 1` |

### Ejemplo Típico

```cpp
FFTProcessor fft;
fft.prepare(4096);  // Inicializar con 4096 samples

// Señal de entrada: 4096 samples de audio
float input[4096];
// ... llenar input ...

// Transformar al dominio de la frecuencia
std::complex<float> spectrum[2049];  // 4096/2 + 1 = 2049 bins
fft.forward(input, spectrum, 4096);

// Manipular el espectro (ej: atenuar agudos)
for (int i = 1000; i < 2049; i++)
    spectrum[i] *= 0.5f;

// Volver al dominio del tiempo
float output[4096];
fft.inverse(spectrum, output, 4096);
// output ahora tiene el audio filtrado
```

---

## 6. Glosario

| Término | Definición |
|---------|-----------|
| **FFT** | Fast Fourier Transform. Algoritmo eficiente para convertir tiempo → frecuencia. |
| **IFFT** | Inverse FFT. Vuelve de frecuencia → tiempo. |
| **Bin** | Cada elemento del espectro de salida. Representa un rango de frecuencias. |
| **Espectro** | Array de números complejos que representan la energía por frecuencia. |
| **Convolución** | Operación que combina dos señales (ej: audio × IR = audio filtrado). |
| **Deconvolución** | Inverso de convolución: dada la salida y la entrada, recuperar la IR. |
| **Overlap-Add** | Método para procesar audio en bloques sin artifacts de clicks. |
| **IR** | Impulse Response. La "firma" acústica de un dispositivo. |
| **pffft** | Librería "Pretty Fast FFT" que usamos para las transformaciones. |
| **SIMD** | Single Instruction Multiple Data. Instrucciones de CPU que procesan varios floats a la vez (SSE, AVX, NEON). |
| **Memoria alineada** | Memoria cuya dirección es múltiplo de 16/32/64 bytes, requerida por SIMD. |
| **Radix-2** | Variante de la FFT que solo funciona con potencias de 2. |
| **Nyquist** | Frecuencia máxima representable: `sampleRate / 2`. A 48 kHz → 24 kHz. |
| **Wiener deconvolution** | Método para dividir espectros de forma robusta evitando división por cero. |
| **Zero-padding** | Rellenar con ceros hasta alcanzar el siguiente tamaño de potencia de 2. |

---

## 7. Notas de Implementación

### Limitaciones conocidas

1. **No es thread-safe:** Cada instancia tiene su propio `workArea`. No se puede usar la misma instancia en dos threads simultáneamente.
2. **malloc/free en forward():** Se allocía un buffer alineado temporal en cada llamada a `forward()`. Podría optimizarse pre-allocándolo en `prepare()`.
3. **Sin windowing integrado:** El windowing (Hanning, Kaiser) se aplica fuera, en `IRPostProcessor`.

### Dependencias

- **pffft** (BSD): La librería FFT subyacente. Obtenida via CMake FetchContent.
- **std::complex<float>**: Complejos estándar de C++, compatible con el formato interleaved de pffft.

### Relación con documentos existentes

- `WINDOWING-NORMALIZATION.md`: Documenta el windowing que se aplica **después** de usar FFTProcessor en el pipeline de deconvolución.
- `SPEC.md` sección 4.4: Menciona FFTProcessor como "Wrapper de pffft".
- `AI-PHASE3.md`: Planifica reutilizar FFTProcessor para STFT en el pipeline de denoising con IA.

---

*Documento v1.0 — Septiembre 2026*
