# Dependencias - DevicesForge

## Herramientas de Build

| Dependencia | Versión Mínima | Mac | Windows | Propósito |
|-------------|----------------|-----|---------|-----------|
| **CMake** | 3.19+ | `brew install cmake` | `choco install cmake` | Build system |
| **Git** | 2.x | `brew install git` | `choco install git` | Control de versiones |
| **Compilador C++** | C++17 | Xcode (Clang) | MSVC 2019/2022 | Compilar código |

## Librerías del Proyecto

| Dependencia | Versión | Licencia | Tamaño aprox. | Propósito |
|-------------|---------|----------|---------------|-----------|
| **VST3 SDK** | 3.7.11+ | MIT | ~5 MB | Framework de plugin |
| **pffft** | latest | BSD | ~200 KB | FFT rápido |
| **ONNX Runtime** | 1.24.x | MIT | ~30-70 MB | Inferencia de IA |
| **Google Test** | latest | BSD | ~1 MB | Unit testing |

## Descargas

### VST3 SDK
- **URL**: https://github.com/steinbergmedia/vst3sdk
- **License**: MIT (gratuito para uso comercial)
- **Tamaño**: ~5 MB (sin compilar)

### ONNX Runtime
- **URL**: https://github.com/microsoft/onnxruntime/releases
- **Paquetes disponibles**:

| Plataforma | Archivo | Tamaño |
|------------|---------|--------|
| macOS ARM | `onnxruntime-osx-arm64-1.24.4.tgz` | 29.5 MB |
| macOS Intel | `onnxruntime-osx-x64-1.24.4.tgz` | ~30 MB |
| Windows x64 | `onnxruntime-win-x64-1.24.4.zip` | 71 MB |
| Windows ARM | `onnxruntime-win-arm64-1.24.4.zip` | 71.7 MB |

### pffft
- **URL**: https://github.com/marton78/pffft
- **Descarga**: Automática via CMake FetchContent
- **Tamaño**: ~200 KB

## Versiones y Compatibilidad

### macOS
- **Mínimo**: macOS 10.14 (Mojave)
- **Recomendado**: macOS 12.0+
- **Arquitecturas**: x86_64, arm64 (Apple Silicon)

### Windows
- **Mínimo**: Windows 10 1809
- **Recomendado**: Windows 11
- **Arquitecturas**: x64, arm64

### Compiladores
| Compilador | Versión Mínima | Notas |
|------------|----------------|-------|
| **Clang** | 10+ | macOS (via Xcode) |
| **MSVC** | 2019+ | Windows |
| **GCC** | 9+ | Linux (soporte futuro) |

## Instalación Rápida

### Mac
```bash
# Ejecutar script de instalación
chmod +x scripts/install-deps.sh
./scripts/install-deps.sh
```

### Windows
```batch
REM Ejecutar script de instalación
scripts\install-deps.bat
```

## Verificación de Instalación

### Verificar CMake
```bash
cmake --version
# Debe mostrar: cmake version 3.19+
```

### Verificar Compilador
```bash
# Mac
clang --version

# Windows (Developer Command Prompt)
cl.exe
```

### Verificar VST3 SDK
```bash
ls external/vst3sdk/CMakeLists.txt
# Debe existir
```

### Verificar ONNX Runtime
```bash
# Mac
ls external/onnxruntime/include/onnxruntime_cxx_api.h

# Windows
dir external\onnxruntime\include\onnxruntime_cxx_api.h
```

## Troubleshooting

### CMake no encontrado
**Mac:**
```bash
brew install cmake
export PATH="/opt/homebrew/bin:$PATH"
```

**Windows:**
```batch
choco install cmake
refreshenv
```

### VST3 SDK no descarga
- Verificar conexión a internet
- Verificar que Git está instalado
- Clonar manualmente: `git clone https://github.com/steinbergmedia/vst3sdk.git external/vst3sdk`

### ONNX Runtime no encontrado
- Descargar manualmente desde GitHub Releases
- Extraer en `external/onnxruntime/`
- Verificar estructura: `external/onnxruntime/include/` y `external/onnxruntime/lib/`

### Errores de compilación
- Verificar que CMake >= 3.19
- Verificar que compilador soporta C++17
- En Windows: usar Developer Command Prompt

---

*Documento v1.0 - Septiembre 2026*
