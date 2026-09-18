# DevicesForge - Documento de Especificación

## 1. Visión General

**DevicesForge** es un plugin VST3 que captura y emula la "huella digital" de **dispositivos de audio físicos** (como un canal de consola analógica, un preamplificador o una cadena concreta de procesado) usando Dynamic Convolution y procesamiento asistido por IA.

## 2. Objetivos

- Capturar curva de EQ y respuesta de fase del dispositivo bajo medición (p. ej. canal de consola, preamp, bus de mezcla)
- Implementar Dynamic Convolución para comportamiento no-lineal
- Usar IA para optimización y denoising de IRs
- Plugin VST3 multiplataforma (macOS / Windows)

## 3. Arquitectura del Sistema

```
┌─────────────────────────────────────────────────────────────┐
│                    VST3 PLUGIN HOST                         │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    │
│  │  Generator  │───▶│  Capture    │───▶│  Dynamic    │    │
│  │  Module     │    │  Module     │    │  Convolver  │    │
│  └─────────────┘    └─────────────┘    └─────────────┘    │
│         │                  │                  │             │
│         ▼                  ▼                  ▼             │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    │
│  │ Sine Sweep  │    │ Multi-Level │    │  FFT (pffft)│    │
│  │ @ -24dB     │    │ IR Storage  │    │  Overlap-   │    │
│  │ @ -12dB     │    │ Ring Buffer │    │  Add        │    │
│  │ @ 0dB       │    │             │    │             │    │
│  │ @ +6dB      │    │             │    │             │    │
│  └─────────────┘    └─────────────┘    └─────────────┘    │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│                    AI HYBRID ENGINE                         │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐              ┌─────────────┐              │
│  │   Local     │              │   Cloud     │              │
│  │   ONNX RT   │◀────────────▶│   API       │              │
│  │   Model     │              │   (Future)  │              │
│  └─────────────┘              └─────────────┘              │
└─────────────────────────────────────────────────────────────┘
```

## 4. Módulos Detallados

### 4.1 Generator Module - Señales de Excitación

| Señal | Niveles | Duración | Uso |
|-------|---------|----------|-----|
| **Sine Sweep** | -24, -12, 0, +6 dB | 1-2s | Medición principal |
| **Dirac Delta** | 0 dB | 1 sample | Respuesta transitoria |
| **Pink Noise** | -12, 0 dB | 2-5s | Verificación |

### 4.2 Capture Module - Adquisición Multi-Nivel

```
Captura Multi-Nivel:
  Sweep @ -24dB ──▶ IR₁ (señal baja)
  Sweep @ -12dB ──▶ IR₂ 
  Sweep @ 0dB   ──▶ IR₃ (nominal)
  Sweep @ +6dB  ──▶ IR₄ (hot)
```

### 4.3 Dynamic Convolution Engine

**Motor de selección e interpolación de IRs:**

1. **Detección de nivel** - RMS de cada sample
2. **Selección** - IR correspondiente al nivel actual
3. **Interpolación** - Suavizado entre IRs adyacentes
4. **Convolución** - Overlap-add FFT

### 4.4 DSP Engine

**Componentes:**
- `FFTProcessor` - Wrapper de pffft (ver [FFT-PROCESSOR.md](FFT-PROCESSOR.md) para documentación detallada)
- `IRManager` - Gestión de IRs multi-nivel
- `DynamicConvolver` - Motor de convolución dinámica

### 4.5 AI Hybrid Engine

**Funciones:**
1. **Denoising** - Eliminar ruido de IR capturada
2. **Extrapolation** - Extender IR más allá de captura
3. **Optimization** - Reducir artefactos

## 5. Formatos de Salida

| Formato | Configuración | Uso |
|---------|---------------|-----|
| **WAV** | 24-bit / 48kHz | Convolution (DAW) |
| **WAV** | 32-bit float / 48kHz | Máxima precisión |
| **Binary** | Custom header | Plugin interno |

## 6. Stack Tecnológico

| Componente | Tecnología | Licencia |
|------------|------------|----------|
| **Framework** | VST3 SDK (nativo) | MIT |
| **DSP** | pffft | BSD |
| **AI Local** | ONNX Runtime | MIT |
| **Build** | CMake | BSD |
| **Tests** | Google Test | BSD |

## 7. Flujo de Trabajo

```
1. SETUP
   └─▶ Conectar dispositivo bajo prueba → Interface de audio
   └─▶ Cargar plugin en DAW
   └─▶ Configurar I/O

2. CAPTURE (Multi-Nivel)
   └─▶ [GENERATE] Sweep @ -24dB → Capturar IR₁
   └─▶ [GENERATE] Sweep @ -12dB → Capturar IR₂
   └─▶ [GENERATE] Sweep @ 0dB   → Capturar IR₃
   └─▶ [GENERATE] Sweep @ +6dB  → Capturar IR₄

3. PROCESS
   └─▶ Deconvolución automática
   └─▶ (Opcional) AI Denoise
   └─▶ Guardar set de IRs

4. PLAYBACK
   └─▶ Dynamic Convolution según nivel de entrada
   └─▶ Interpolación suave entre niveles
```

## 8. Requisitos de Precisión

| Métrica | Objetivo |
|---------|----------|
| **Respuesta en Freq** | ±0.5 dB (20Hz-20kHz) |
| **Relación S/R** | >60 dB |
| **Resolución Temporal** | <1ms |
| **THD+N** | <-80 dB |

## 9. Roadmap

### Fase 1: Estructura (Semana 1)
- [x] Directorios y archivos
- [x] CMakeLists.txt
- [x] .gitignore

### Fase 2: Plugin Core (Semanas 2-3)
- [ ] PluginProcessor funcional
- [ ] PluginEditor básico
- [ ] Parámetros VST3
- [x] Múltiples señales de excitación (Sweep / Dirac / Pink / MLS)

### Fase 3: DSP Engine (Semanas 4-6)
- [ ] FFTProcessor con pffft
- [ ] IRManager multi-nivel
- [ ] DynamicConvolver

### Fase 4: AI (Semanas 7-9)
- [ ] Integrar ONNX Runtime
- [ ] Modelo de denoising
- [ ] Optimización

### Fase 5: Polish (Semanas 10-12)
- [ ] UI profesional
- [ ] Presets
- [ ] Testing completo

## 10. Referencias

- **VST3 SDK**: https://github.com/steinbergmedia/vst3sdk
- **pffft**: https://github.com/marton78/pffft
- **ONNX Runtime**: https://onnxruntime.ai
- **Dynamic Convolution**: Patent US7039194B1 (Expired 2017)
- **Sweep deconvolution**: Farina, A. (2000)

---

*Documento v1.1 - Septiembre 2026*
