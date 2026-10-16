# DevicesForge

Plugin **VST3** para capturar y emular la huella digital (respuesta al impulso) de **dispositivos de audio** —mesas analógicas, preamps, cadenas de insert, etc.— usando **convolución dinámica**.

| | |
|---|---|
| **Versión** | 1.0.0 |
| **Formato** | VST3 (estéreo in / estéreo out) |
| **Sample rate de diseño** | 48 kHz (`SAMPLE_RATE_DEFAULT`) |
| **Plataformas** | macOS (Apple Silicon / Intel), Windows (objetivo) |
| **Licencias de terceros** | VST3 SDK (MIT), pffft (BSD), Google Test (BSD) |

Documentación detallada en [`docs/`](docs/):

- [`docs/SPEC.md`](docs/SPEC.md) — arquitectura, módulos y roadmap
- [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md) — herramientas y librerías
- [`docs/CAPTURE-WORKFLOW.md`](docs/CAPTURE-WORKFLOW.md) — flujo de captura en estudio (diseño)
- [`docs/CAPTURE-RING-BUFFER.md`](docs/CAPTURE-RING-BUFFER.md) — ring buffer, pre/post-trigger y disparo con Generate
- [`docs/WINDOWING-NORMALIZATION.md`](docs/WINDOWING-NORMALIZATION.md) — ventaneo Hanning/Kaiser y normalización de pico de IR
- [`docs/DYNAMIC-CONVOLUTION-MIX.md`](docs/DYNAMIC-CONVOLUTION-MIX.md) — DynamicConvolver, Mix dry/wet e integración prevista en `process()`
- [`docs/EXPORT-FORMATS.md`](docs/EXPORT-FORMATS.md) — WAV / AIFF / DFIR y carpeta `exports/latest`

---

## Qué hace (visión)

DevicesForge está pensado para:

1. **Generar** señales de excitación (sweep, ruido, etc.) desde el DAW.
2. **Capturar** la respuesta del dispositivo bajo prueba (p. ej. un canal de consola) a varios niveles (−24, −12, 0, +6 dB).
3. **Procesar** IRs (deconvolución, ventaneo, export WAV).
4. **Reproducir** audio con **convolución dinámica** (selección e interpolación de IR según nivel de entrada).

```
Generator → Capture → IR multi-nivel → DynamicConvolver → salida
```

---

## Estado actual del código

El repositorio compila un VST3 válido (pasa el **validator** del VST3 SDK). Parte del DSP y de los parámetros están implementados pero **aún no cableados** al path de audio del plugin.

| Área | Estado |
|------|--------|
| VST3 processor / controller, parámetros (Mix, Gain, IR, Signal, Duration, Generate) | **Gain**, **Generate**, **Mix** e **IR** afectan el audio |
| `FFTProcessor`, `IRManager`, `DynamicConvolver` | Convolución particionada (overlap-save) **cableada en `process()`**: la pista suena a través de la IR capturada |
| `SignalGenerator` | Sweep log, Dirac, pink noise, MLS; cableado a `process()` |
| `RingCaptureBuffer` | Ring estéreo + captura mono; **disparo automático** al activar Generate |
| `SweepDeconvolver` + `IRPostProcessor` | Deconvolución FFT al completar captura; Hanning + peak norm → `IRManager` |
| `IRExporter` | Auto-export a `~/Documents/DevicesForge/exports/latest/`; archivar con **SaveIR** / **LoadIR** |
| Editor gráfico (VSTGUI) | **Sí** (mínimo: Send, Generate, InPeak, Mix, Gain, Save/Load) |

Para probar hoy: **Generate** con retorno cableado → revisar archivos en [`docs/EXPORT-FORMATS.md`](docs/EXPORT-FORMATS.md). Ver [Probar en el DAW](#probar-en-el-daw).

---

## Estructura del proyecto

```
DevicesForge/
├── libs/                # Código común reutilizable por cualquier producto
│   ├── dfcore/          # Sin VST: FFT, convolución, IR/WAV I/O, DFConstants.h
│   ├── dfmeasure/       # Sin VST: barrido, captura, deconvolución, post-proceso
│   └── dfvst/           # Helpers de plugin: diálogos nativos de archivo
├── products/
│   └── capturer-vst/    # VST3 de captura actual (DevicesForge.vst3)
├── host/                # Host CLI mínimo (carga .vst3, genera WAV de prueba)
├── tests/               # Google Test — DSP aislado
├── docs/                # Especificación y guías
├── scripts/             # install-deps.sh / .bat
├── external/            # vst3sdk (no versionado; ver install-deps)
├── build.sh             # Configura CMake y compila
└── cpvst.sh             # Copia DevicesForge.vst3 a ~/Library/.../VST3
```

**Targets CMake principales**

| Target | Descripción |
|--------|-------------|
| `DevicesForge` | Plugin VST3 de captura (`products/capturer-vst`) |
| `dfcore` | Librería estática: FFT, convolución, IR/WAV I/O |
| `dfmeasure` | Librería estática: medición y deconvolución (usa `dfcore`) |
| `dfvst` | Librería estática: diálogos de archivo (usa `dfcore`) |
| `DevicesForgeHost` | Host de línea de comandos |
| `DevicesForgeTests` | Tests (`ctest`) |

---

## Requisitos

- **CMake** ≥ 3.19  
- **C++17** (Xcode CLT en macOS, MSVC 2019+ en Windows)  
- **Git** (FetchContent: pffft, Google Test)  
- **VST3 SDK** en `external/` (script de instalación)

Detalle: [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md).

---

## Instalación de dependencias

### macOS

```bash
chmod +x scripts/install-deps.sh
./scripts/install-deps.sh
```

Instala CMake/Git si hace falta y clona el VST3 SDK en `external/`.

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

---

## Instalar en el DAW

```bash
./cpvst.sh              # Copia Release → ~/Library/Audio/Plug-Ins/VST3/
./cpvst.sh --build      # build.sh --plugin-only + copia
./cpvst.sh --link       # Symlink al build (desarrollo)
./cpvst.sh --system     # /Library/Audio/Plug-Ins/VST3/ (sudo)
```

**Cubase:** Studio → VST Plug-in Manager → engranaje → **Rescan All**.  
Buscar **DevicesForge** (subcategoría VST3 actual: `"Fx"` — puede aparecer en *Otros*; ver [`PluginFactory.cpp`](products/capturer-vst/PluginFactory.cpp) y constantes `kFx|…` en el SDK).

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
| 1003 | Gain | −12 … +12 dB: volumen de la **emulación** (después del Mix) |
| 1016 | Send | −12 … +12 dB: nivel del **sweep** hacia el dispositivo (solo Generate) |
| 1001 | IR | Selección de IR 0–3 (multi-nivel) |
| 1005 | Signal | Sweep / Dirac / Pink / MLS (**activo** con Generate) |
| 1006 | Duration | 0.5–5.0 s (Dirac ignora este valor) |
| 1007 | Generate | Off→On dispara sweep + captura; **vuelve a Off** al terminar el sweep; apaga Cal |
| 1014 | Cal | On: tono/sweep de calibración **sin captura** (ajustar loop con InPeak) |
| 1015 | CalSig | **1kHz** continuo o **Sweep** en bucle (usa Duration) |
| 1008 | ExportFmt | Oculto: formato del auto-export a `latest/` |
| 1009 | Export | Oculto: reexportar a `latest/` (usar SaveIR) |
| 1017 | SaveIR | Diálogo: guardar la IR en la carpeta que elijas (WAV float) |
| 1018 | LoadIR | Diálogo: cargar una IR guardada |
| 1010 | InPeak | Solo lectura: pico de entrada en **dBFS** (−60…0), con peak-hold |
| 1011 | ClrLatest | Oculto; On: borra `exports/latest/` al iniciar cada Generate |
| 1012 | Version | Solo lectura: build (`FULL_VERSION_STR` en `version.h`) |
| 1013 | IRLen | Solo lectura: largo de la IR en memoria (**0 ms = sin IR**, passthrough) |

Constantes en [`products/capturer-vst/DevicesForge.h`](products/capturer-vst/DevicesForge.h).

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

La subcategoría VST3 se define en `products/capturer-vst/PluginFactory.cpp` (`subCategories` en `DEF_CLASS2`).  
Valor genérico `"Fx"` suele listarse como *Otros*. Para ubicarlo mejor (p. ej. **Channel Strip** o **Reverb**), usar las constantes de `pluginterfaces/vst/ivstaudioprocessor.h` (`kFxChannelStrip`, `kFxReverb`, etc.).

---

## Roadmap (resumen)

Checklist completo en [`SPEC.md`](SPEC.md) §10.

| Fase | Estado | Enfoque |
|------|--------|---------|
| 1–2 | Hecho | Captura, deconvolución, export, Cubase |
| **3** | **Ahora** | **Convolución en el plugin**, A/B con hardware, validar el concepto |
| 4 | Después | UI, presets, automatización, QA |

**Hecho:** `DynamicConvolver` + **Mix** + **IR Select** cableados en `process()`. **Próximo paso:** pruebas A/B en estudio (hardware vs emulación) y documentar resultados.

---

## Referencias

- [VST3 SDK](https://github.com/steinbergmedia/vst3sdk)  
- [pffft](https://github.com/marton78/pffft)  
- Deconvolución por sweep: Farina, A. (2000)

---

## Autor

**Leandro Sayanes** — contacto en [`products/capturer-vst/version.h`](products/capturer-vst/version.h).
