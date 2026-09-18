# DevicesForge

Plugin **VST3** para capturar y emular la huella digital (respuesta al impulso) de **dispositivos de audio** —mesas analógicas, preamps, cadenas de insert, etc.— usando **convolución dinámica** y un motor de IA local basado en **ONNX Runtime**.

| | |
|---|---|
| **Versión** | 1.0.0 |
| **Formato** | VST3 (estéreo in / estéreo out) |
| **Sample rate de diseño** | 48 kHz (`SAMPLE_RATE_DEFAULT`) |
| **Plataformas** | macOS (Apple Silicon / Intel), Windows (objetivo) |
| **Licencias de terceros** | VST3 SDK (MIT), pffft (BSD), ONNX Runtime (MIT), Google Test (BSD) |

Documentación detallada en [`docs/`](docs/):

- [`docs/SPEC.md`](docs/SPEC.md) — arquitectura, módulos y roadmap
- [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md) — herramientas y librerías
- [`docs/CAPTURE-WORKFLOW.md`](docs/CAPTURE-WORKFLOW.md) — flujo de captura en estudio (diseño)

---

## Qué hace (visión)

DevicesForge está pensado para:

1. **Generar** señales de excitación (sweep, ruido, etc.) desde el DAW.
2. **Capturar** la respuesta del dispositivo bajo prueba (p. ej. un canal de consola) a varios niveles (−24, −12, 0, +6 dB).
3. **Procesar** IRs (deconvolución, ventaneo, export WAV).
4. **Reproducir** audio con **convolución dinámica** (selección e interpolación de IR según nivel de entrada).
5. **Refinar** IRs con IA local (denoise / optimización vía ONNX).

```
Generator → Capture → IR multi-nivel → DynamicConvolver (+ ONNX opcional) → salida
```

---

## Estado actual del código

El repositorio compila un VST3 válido (pasa el **validator** del VST3 SDK). Parte del DSP y de los parámetros están implementados pero **aún no cableados** al path de audio del plugin.

| Área | Estado |
|------|--------|
| VST3 processor / controller, parámetros (Mix, Gain, IR, AI, Signal, Duration, Generate) | **Gain** y **Generate** afectan el audio; Mix/IR/AI se leen pero no procesan |
| `FFTProcessor`, `IRManager`, `DynamicConvolver` | Implementados; tests unitarios; convolver **no** llamado desde `process()` |
| `SignalGenerator` | Sweep log, Dirac, pink noise, MLS; cableado a `process()` |
| Captura / UI | Diseño en docs; **pendiente** |
| `ONNXInference` | Enlazado si hay ONNX Runtime; **sin** carga de modelo en runtime |
| Editor gráfico (VSTGUI) | **No** |

Para probar hoy: cargar el plugin en un DAW (p. ej. Cubase) y usar el parámetro **Gain** (−12 … +12 dB). Ver sección [Probar en el DAW](#probar-en-el-daw).

---

## Estructura del proyecto

```
DevicesForge/
├── src/
│   ├── plugin/          # VST3: Processor, Factory, CIDs, DevicesForge.h
│   ├── dsp/             # FFTProcessor, IRManager, DynamicConvolver
│   └── ai/              # ONNXInference (opcional, HAS_ONNX_RUNTIME)
├── host/                # Host CLI mínimo (carga .vst3, genera WAV de prueba)
├── tests/               # Google Test — DSP aislado
├── docs/                # Especificación y guías
├── scripts/             # install-deps.sh / .bat
├── external/            # vst3sdk, onnxruntime (no versionados; ver install-deps)
├── build.sh             # Configura CMake y compila
└── cpvst.sh             # Copia DevicesForge.vst3 a ~/Library/.../VST3
```

**Targets CMake principales**

| Target | Descripción |
|--------|-------------|
| `DevicesForge` | Plugin VST3 |
| `DevicesForgeDSP` | Librería estática DSP (tests) |
| `DevicesForgeHost` | Host de línea de comandos |
| `DevicesForgeTests` | Tests (`ctest`) |

---

## Requisitos

- **CMake** ≥ 3.19  
- **C++17** (Xcode CLT en macOS, MSVC 2019+ en Windows)  
- **Git** (FetchContent: pffft, Google Test)  
- **VST3 SDK** y **ONNX Runtime** en `external/` (script de instalación)

Detalle: [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md).

---

## Instalación de dependencias

### macOS

```bash
chmod +x scripts/install-deps.sh
./scripts/install-deps.sh
```

Instala CMake/Git si hace falta, clona el VST3 SDK y descarga ONNX Runtime 1.24.x en `external/`.

### Windows

```batch
scripts\install-deps.bat
```

---

## Compilar

### Script recomendado

```bash
./build.sh                  # Release, tests + host + plugin
./build.sh --plugin-only    # Solo el VST3 (más rápido)
./build.sh --debug          # Build Debug
./build.sh --clean          # Borra build/ y reconfigura
./build.sh --test           # Compila y ejecuta ctest
./build.sh --full-sdk       # Incluye samples del VST3 SDK (lento)
```

**Salida del plugin:** `build/VST3/Release/DevicesForge.vst3`  
En macOS, CMake puede crear un symlink en  
`~/Library/Audio/Plug-Ins/VST3/DevicesForge.vst3` → build.

### CMake manual

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
ctest --output-on-failure   # opcional
```

Si ONNX Runtime no está en `external/onnxruntime`, el proyecto **compila igual** pero define `HAS_ONNX_RUNTIME` solo cuando encuentra headers y librería.

---

## Instalar en el DAW

```bash
./cpvst.sh              # Copia Release → ~/Library/Audio/Plug-Ins/VST3/
./cpvst.sh --build      # build.sh --plugin-only + copia
./cpvst.sh --link       # Symlink al build (desarrollo)
./cpvst.sh --system     # /Library/Audio/Plug-Ins/VST3/ (sudo)
```

**Cubase:** Studio → VST Plug-in Manager → engranaje → **Rescan All**.  
Buscar **DevicesForge** (subcategoría VST3 actual: `"Fx"` — puede aparecer en *Otros*; ver [`PluginFactory.cpp`](src/plugin/PluginFactory.cpp) y constantes `kFx|…` en el SDK).

---

## Probar en el DAW

1. `./build.sh --plugin-only && ./cpvst.sh`
2. Abrir el proyecto a **48 kHz** (recomendado; ver spec).
3. Insertar **DevicesForge** en una pista con audio.
4. Ajustar **Gain** y comprobar nivel de salida.

Flujo de captura multi-nivel y convolución en tiempo real: ver roadmap en [`docs/SPEC.md`](docs/SPEC.md).

---

## Tests

```bash
./build.sh --test
# o
cd build && ctest --output-on-failure
```

Los tests en `tests/test_DSP.cpp` cubren FFT, gestión de IR y convolver **fuera** del binario VST3.

**Host CLI** (opcional):

```bash
./build/bin/DevicesForgeHost
# o con ruta explícita:
./build/bin/DevicesForgeHost /ruta/a/DevicesForge.vst3
```

Genera `test_output.wav` con un seno de prueba (útil para humo, no sustituye el DAW).

---

## Parámetros VST3

| ID | Nombre | Descripción |
|----|--------|-------------|
| 1002 | Mix | 0–100 % (reservado para dry/wet convolver) |
| 1003 | Gain | −12 … +12 dB (**activo** en `process()`) |
| 1001 | IR | Selección de IR 0–3 (multi-nivel) |
| 1004 | AI | Denoise IA (on/off) |
| 1005 | Signal | Sweep / Dirac / Pink / MLS (**activo** con Generate) |
| 1006 | Duration | 0.5–5.0 s (Dirac ignora este valor) |
| 1007 | Generate | On/off; flanco Off→On dispara un one-shot de la señal |

Constantes en [`src/plugin/DevicesForge.h`](src/plugin/DevicesForge.h).

---

## Configuración DSP (constantes)

| Constante | Valor | Uso |
|-----------|-------|-----|
| `FFT_SIZE` | 4096 | Bloques FFT / overlap-add |
| `MAX_IR_LENGTH` | 65536 | Límite de IR en memoria |
| `NUM_CAPTURE_LEVELS` | 4 | Niveles −24, −12, 0, +6 dB |
| `SAMPLE_RATE_DEFAULT` | 48000 | Captura, WAV, defaults |

En runtime el plugin usa `processSetup.sampleRate` del host al activar la pista.

---

## Categoría del plugin en el host

La subcategoría VST3 se define en `src/plugin/PluginFactory.cpp` (`subCategories` en `DEF_CLASS2`).  
Valor genérico `"Fx"` suele listarse como *Otros*. Para ubicarlo mejor (p. ej. **Channel Strip** o **Reverb**), usar las constantes de `pluginterfaces/vst/ivstaudioprocessor.h` (`kFxChannelStrip`, `kFxReverb`, etc.).

---

## Roadmap (resumen)

Ver checklist completo en [`docs/SPEC.md`](docs/SPEC.md):

- Fase 2: processor completo, editor, parámetros conectados  
- Fase 3: convolver en `process()`, captura multi-nivel  
- Fase 4: modelo ONNX de denoise  
- Fase 5: UI, presets, QA  

---

## Referencias

- [VST3 SDK](https://github.com/steinbergmedia/vst3sdk)  
- [pffft](https://github.com/marton78/pffft)  
- [ONNX Runtime](https://onnxruntime.ai)  
- Deconvolución por sweep: Farina, A. (2000)

---

## Autor

**lsayanes & DevicesForge** — contacto en [`src/plugin/version.h`](src/plugin/version.h).
