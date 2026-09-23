# DevicesForge - Especificación (resumen técnico)

**DevicesForge** es un plugin VST3 que captura y emula la "huella digital" de **dispositivos de audio físicos** (como un canal de consola analógica, un preamplificador o una cadena concreta de procesado) usando Dynamic Convolution y, opcionalmente, procesamiento asistido por IA.

Documento raíz más completo: [`../SPEC.md`](../SPEC.md).

## Objetivos

- Capturar curva de EQ y respuesta de fase del dispositivo bajo medición
- Generar IRs de alta precisión (24-bit / 48 kHz)
- Emular el dispositivo en el DAW vía convolución dinámica
- (Fase 5, opcional) Denoise de IR con IA local si las capturas lo justifican

## 9. Roadmap (alineado con SPEC raíz)

### Fase 1: Estructura — hecho
- [x] Directorios, CMake, .gitignore

### Fase 2: Captura y procesado — hecho
- [x] Señales de excitación (Sweep / Dirac / Pink / MLS)
- [x] Ring buffer + pre-trigger, deconvolución, ventaneo, export
- [x] Parámetros Generate, Gain, Export, InPeak

### Fase 3: Emulación y validación (actual)
Ver [`DYNAMIC-CONVOLUTION-MIX.md`](DYNAMIC-CONVOLUTION-MIX.md).

- [x] DynamicConvolver en el path de audio del plugin (overlap-save particionado)
- [x] Mix / IR Select operativos
- [ ] Pruebas A/B hardware vs plugin; documentar resultados
- [ ] (Opcional) IR multi-nivel + interpolación
- [ ] Cargar IR desde archivo

### Fase 4: Polish
- [ ] PluginEditor (VSTGUI)
- [ ] Presets, automatización, QA en DAW

### Fase 5: IA opcional (congelada)
- Laboratorio: [`AI-PHASE3.md`](AI-PHASE3.md), [`AI-PHASE3-GUIA.md`](AI-PHASE3-GUIA.md), carpeta [`../ml/`](../ml/)
- Integración C++ y param **AI** — solo tras go/no-go en Fase 3

## 10. Referencias

- **VST3 SDK**: https://github.com/steinbergmedia/vst3sdk
- **pffft**: https://github.com/marton78/pffft
- **ONNX Runtime**: https://onnxruntime.ai (Fase 5)
- **Sweep deconvolution**: Farina, A. (2000)

---

*Documento v1.2 — Septiembre 2026*
