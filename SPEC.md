# DevicesForge - Documento de Especificación (borrador)

## 1. Visión General

Software VST3 plugin que captura la "huella digital" (Impulse Response) de **dispositivos de audio físicos** (como un canal de consola analógica, un preamplificador o una cadena de procesado), permitiendo su uso en convolución digital para emular ese dispositivo.

## 2. Objetivos

- Capturar curva de EQ y respuesta de fase del dispositivo bajo medición (p. ej. canal de consola, preamp)
- Generar IRs de alta precisión (24-bit / 48kHz)
- Usar IA para optimización y generación de IRs
- Plugin VST3 multiplataforma (macOS / Windows)

## 3. Arquitectura del Sistema

```
┌─────────────────────────────────────────────────────────────┐
│                    VST3 PLUGIN HOST                         │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    │
│  │  Generator  │───▶│  Capture    │───▶│   Process   │    │
│  │  Module     │    │  Module     │    │   Module    │    │
│  └─────────────┘    └─────────────┘    └─────────────┘    │
│         │                  │                  │             │
│         ▼                  ▼                  ▼             │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    │
│  │ Sine Sweep  │    │ Audio Input │    │  FFT/Deconv │    │
│  │ Dirac Delta │    │ Buffer      │    │  AI Engine  │    │
│  │ Pink Noise  │    │ Ring Buffer │    │  IR Export  │    │
│  └─────────────┘    └─────────────┘    └─────────────┘    │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│                    AI HYBRID ENGINE                         │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐              ┌─────────────┐              │
│  │   Local     │              │   Cloud     │              │
│  │   ONNX RT   │◀────────────▶│   API       │              │
│  │   Model     │              │   (Optional)│              │
│  └─────────────┘              └─────────────┘              │
└─────────────────────────────────────────────────────────────┘
```

## 4. Módulos Detallados

### 4.1 Generator Module - Señales de Excitación

| Señal | Frecuencia | Duración | Uso |
|-------|------------|----------|-----|
| **Sine Sweep** | 20Hz - 20kHz (log) | 1-2s | Medición principal |
| **Dirac Delta** | Broadband | 1 sample | Respuesta瞬态 |
| **Pink Noise** | 20Hz - 20kHz | 2-5s | Verificación |
| **MLS** | Broadband | 1-2s | Alta relación S/R |

**Especificaciones:**
- Generación en buffer a 64-bit float internamente
- Output a 24-bit / 48kHz (configurable)
- Sync con clock del DAW via VST3

### 4.2 Capture Module - Adquisición

```
Input Chain:
  Line In (Device) ──▶ ADC ──▶ Ring Buffer ──▶ Circular Record
                                                        │
                                          ┌─────────────┴─────────────┐
                                          ▼                           ▼
                                    Pre-trigger                Post-trigger
                                    (100ms)                    (1000ms)
```

**Configuración:**
- Buffer size: 512 samples (adjustable)
- Latencia reportada al DAW: < 10ms
- Formato interno: 64-bit float
- Record: Circular buffer con pre/post trigger

### 4.3 Process Module - Deconvolución

**Pipeline:**
1. **Alignment** - Cross-correlación para alinear sweep/ref
2. **Deconvolución FFT** - Extracción de IR
3. **Windowing** - Aplicación de ventana (Hanning/Kaiser)
4. **Normalización** - Peak normalization
5. **Export** - WAV 24/48 o formato binario

**Algoritmos:**
- Sweep deconvolución: `IR = IFFT(FFT(recorded) ./ FFT(sweep_reference))`
- Compensación de fase
- Zero-padding para resolución espectral

### 4.4 AI Hybrid Engine

**Funciones de IA:**
1. **Denoising** - Eliminación de ruido de la IR capturada
2. **Extrapolation** - Extender IR más allá de la captura
3. **Optimization** - Reducir artefactos de medición
4. **Style Transfer** - Transferir características entre dispositivos capturados

**Modelo Local (ONNX Runtime):**
- Modelo entrenado con pares de IRs (ruido/limpio)
- Input: IR cruda (2048 samples)
- Output: IR optimizada
- Tamaño: ~50MB inferencia en CPU

**API Cloud (Opcional):**
- Endpoint para procesamiento pesado
- Modelo más grande y preciso
- Fallback cuando local no es suficiente

## 5. Formatos de Salida

| Formato | Configuración | Uso |
|---------|---------------|-----|
| **WAV** | 24-bit / 48kHz | Convolution (DAW) |
| **WAV** | 32-bit float / 48kHz | Máxima precisión |
| **Binary** | Custom header | Plugin interno |
| **AIFF** | 24-bit / 96kHz | macOS Pro Tools |

## 6. Interfaz de Usuario

```
┌────────────────────────────────────────────────────────────┐
│  CONSOLE IR CAPTURE v1.0                                   │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  [GENERATE]    Signal: [Sine Sweep ▼]  Duration: [1.0s]  │
│                                                            │
│  ┌────────────────────────────────────────────────────┐   │
│  │  ███████████████████████░░░░░░░░░░░░░░░░░░░░░░░  │   │
│  │  0.0s                              1.0s           │   │
│  └────────────────────────────────────────────────────┘   │
│                                                            │
│  [CAPTURE]     Input: [Console L ▼]  Gain: [-6.0 dB]     │
│                                                            │
│  ┌────────────────────────────────────────────────────┐   │
│  │  ░░░░░░░░░░░░░████████████████████████████████░░░ │   │
│  │  Peak: -3.2 dBFS     Buffer: 48.2%                 │   │
│  └────────────────────────────────────────────────────┘   │
│                                                            │
│  [PROCESS]     Method: [Deconvolution ▼]                  │
│                AI: [Local ▼]                              │
│                                                            │
│  ┌────────────────────────────────────────────────────┐   │
│  │  IR Preview:                                       │   │
│  │  ▁▃▇█▇▃▁▂▄▆█▆▄▂▁▃▇█▇▃▁                            │   │
│  │  -60dB ◀────────────────────────────────▶ 0dB      │   │
│  └────────────────────────────────────────────────────┘   │
│                                                            │
│  [EXPORT WAV]  [SAVE IR]  [LOAD IR]                       │
│                                                            │
│  Status: Ready          Latency: 8.3ms                     │
└────────────────────────────────────────────────────────────┘
```

## 7. Stack Tecnológico

| Componente | Tecnología | Justificación |
|------------|------------|---------------|
| **Framework** | VST3 SDK | Estándar profesional, cross-platform |
| **Audio** | JUCE (wrapper) | Facilita desarrollo VST3 |
| **DSP** | Kiss FFT / pffft | Rápido, libre, optimizado |
| **AI Local** | ONNX Runtime | Inferencia multiplataforma |
| **AI Cloud** | REST API | Flexibilidad, modelo pesado |
| **UI** | VSTGUI / iPlug2 | Integración nativa VST3 |
| **Build** | CMake | Cross-platform builds |
| **Tests** | Google Test | Unit testing robusto |

## 8. Flujo de Trabajo del Usuario

```
1. SETUP
   └─▶ Conectar dispositivo bajo prueba → Interface de audio
   └─▶ Cargar plugin en DAW
   └─▶ Configurar I/O

2. CAPTURE
   └─▶ Seleccionar tipo de señal (Sweep)
   └─▶ Ajustar nivel de ganancia
   └─▶ Presionar [GENERATE] → Reproduce sweep
   └─▶ Plugin captura automáticamente
   └─▶ [CAPTURE] graba resultado

3. PROCESS
   └─▶ Deconvolución automática
   └─▶ (Opcional) Activar AI para optimización
   └─▶ Preview de IR generada

4. EXPORT
   └─▶ Guardar IR como WAV
   └─▶ Cargar en convolucionador
   └─▶ Comparar con el dispositivo original
```

## 9. Requisitos de Precisión

| Métrica | Objetivo | Método |
|---------|----------|--------|
| **Respuesta en Freq** | ±0.5 dB (20Hz-20kHz) | Sweep calibrado |
| **Relación S/R** | >60 dB | Promediación |
| **Resolución Temporal** | <1ms | Zero-padding FFT |
| **Precisión de Fase** | <5° | Calibration sweep |
| **THD+N** | <-80 dB | Señal limpia |

## 10. Roadmap

### Fase 1: MVP (Semanas 1-4)
- [x] Setup proyecto VST3 + CMake
- [x] Generator: Sine sweep básico
- [x] Capture: Record a WAV
- [x] Process: Deconvolución FFT simple
- [x] UI: Botones básicos

### Fase 2: Core (Semanas 5-8)
- [x] Múltiples señales de excitación
- [x] Ring buffer con pre-trigger
- [x] Windowing y normalización
- [x] Export multi-formato

### Fase 3: AI (Semanas 9-12)
- [ ] Entrenar modelo de denoising (STFT U-Net → ONNX); ver [`docs/AI-PHASE3.md`](docs/AI-PHASE3.md)
- [ ] Integrar ONNX Runtime en post-captura (STFT + overlap-add, param **AI**)
- [ ] ~~API cloud fallback~~ — pospuesto hasta IA local estable
- [ ] Denoise de IR (MVP); extrapolación / style transfer fuera de alcance inicial

### Fase 4: Polish (Semanas 13-16)
- [ ] UI profesional
- [ ] Presets por tipo de dispositivo
- [ ] Automatización
- [ ] Testing completo

## 11. Referencias

- **VST3 SDK**: https://github.com/steinbergmedia/vst3sdk
- **JUCE**: https://juce.com
- **ONNX Runtime**: https://onnxruntime.ai
- **Sweep deconvolution**: Farina, A. (2000)
- **IR formats**: OpenAIR library

---

*Documento v1.0 - Septiembre 2026*
