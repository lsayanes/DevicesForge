#!/bin/bash
# ============================================================================
# DevicesForge - Script de build
# ============================================================================
#
# Uso:
#   ./build.sh                 # Release (default)
#   ./build.sh --debug         # Debug
#   ./build.sh --clean         # Borra build/ y reconfigura
#   ./build.sh --test          # Compila y corre ctest
#   ./build.sh --plugin-only   # Solo el VST3 (sin tests ni host)
#   ./build.sh --full-sdk      # Incluye samples del VST3 SDK
#   ./build.sh -- -j4          # Extra args para cmake --build
#

set -euo pipefail

WORKDIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$WORKDIR/build"
BUILD_TYPE="Release"
CLEAN=0
RUN_TESTS=0
BUILD_TESTS=ON
BUILD_TEST_HOST=ON
SDK_EXAMPLES=OFF
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
EXTRA_BUILD_ARGS=()

usage() {
    cat <<'EOF'
DevicesForge - build.sh

Opciones:
  --debug           CMAKE_BUILD_TYPE=Debug
  --release         CMAKE_BUILD_TYPE=Release (default)
  --clean           Elimina el directorio build/ antes de configurar
  --test            Ejecuta ctest al terminar
  --plugin-only     No compila tests ni el host CLI
  --full-sdk        Compila también los samples del VST3 SDK
  -j N              Jobs de compilación (default: núcleos de CPU)
  -h, --help        Esta ayuda

Todo lo que va después de -- se pasa a cmake --build.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug) BUILD_TYPE="Debug"; shift ;;
        --release) BUILD_TYPE="Release"; shift ;;
        --clean) CLEAN=1; shift ;;
        --test) RUN_TESTS=1; shift ;;
        --plugin-only)
            BUILD_TESTS=OFF
            BUILD_TEST_HOST=OFF
            shift
            ;;
        --full-sdk) SDK_EXAMPLES=ON; shift ;;
        -j)
            JOBS="$2"
            shift 2
            ;;
        -j*)
            JOBS="${1#-j}"
            shift
            ;;
        -h|--help) usage; exit 0 ;;
        --)
            shift
            EXTRA_BUILD_ARGS+=("$@")
            break
            ;;
        *)
            echo "Opción desconocida: $1" >&2
            usage
            exit 1
            ;;
    esac
done

echo "============================================"
echo "  DevicesForge - Build ($BUILD_TYPE)"
echo "============================================"

# ============================================================================
# Prechecks
# ============================================================================
if ! command -v cmake >/dev/null 2>&1; then
    echo "Error: cmake no está en PATH." >&2
    echo "Instalá dependencias con: ./scripts/install-deps.sh" >&2
    exit 1
fi

if [[ ! -f "$WORKDIR/external/vst3sdk/CMakeLists.txt" ]]; then
    echo "Error: VST3 SDK no encontrado en external/vst3sdk/" >&2
    echo "Ejecutá: ./scripts/install-deps.sh" >&2
    exit 1
fi

if [[ ! -d "$WORKDIR/external/onnxruntime/include" ]]; then
    echo "Aviso: ONNX Runtime no encontrado — las features de IA se deshabilitan."
fi

# ============================================================================
# Configure
# ============================================================================
if [[ "$CLEAN" -eq 1 && -d "$BUILD_DIR" ]]; then
    echo ""
    echo "[1/3] Limpiando $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
else
    echo ""
    echo "[1/3] Directorio de build: $BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

echo ""
echo "[2/3] Configurando CMake..."
cmake -S "$WORKDIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DBUILD_TESTS="$BUILD_TESTS" \
    -DBUILD_TEST_HOST="$BUILD_TEST_HOST" \
    -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES="$SDK_EXAMPLES" \
    -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES="$SDK_EXAMPLES"

# ============================================================================
# Build
# ============================================================================
echo ""
echo "[3/3] Compilando (jobs=$JOBS)..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$JOBS" "${EXTRA_BUILD_ARGS[@]+"${EXTRA_BUILD_ARGS[@]}"}"

# ============================================================================
# Tests
# ============================================================================
if [[ "$RUN_TESTS" -eq 1 ]]; then
    if [[ "$BUILD_TESTS" != "ON" ]]; then
        echo "Aviso: --test ignorado porque se usó --plugin-only." >&2
    else
        echo ""
        echo "Ejecutando tests..."
        ctest --test-dir "$BUILD_DIR" --output-on-failure --build-config "$BUILD_TYPE"
    fi
fi

# ============================================================================
# Resumen
# ============================================================================
echo ""
echo "============================================"
echo "  Build completado ($BUILD_TYPE)"
echo "============================================"
echo ""
echo "Artefactos:"
echo "  Plugin:  $BUILD_DIR/VST3/$BUILD_TYPE/DevicesForge.vst3"
if [[ "$BUILD_TEST_HOST" == "ON" ]]; then
    echo "  Host:    $BUILD_DIR/bin/DevicesForgeHost"
fi
if [[ "$BUILD_TESTS" == "ON" ]]; then
    echo "  Tests:   $BUILD_DIR/bin/DevicesForgeTests  (o ctest --test-dir build)"
fi
echo ""
if [[ "$RUN_TESTS" -eq 0 && "$BUILD_TESTS" == "ON" ]]; then
    echo "Para correr tests:  ./build.sh --test"
    echo ""
fi
