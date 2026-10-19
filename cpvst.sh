#!/bin/bash
# ============================================================================
# DevicesForge - Copiar VST3 al folder que usa Cubase (y otros hosts)
# ============================================================================
#
# Cubase escanea por defecto (macOS, VST3):
#   ~/Library/Audio/Plug-Ins/VST3/          (usuario, recomendado)
#   /Library/Audio/Plug-Ins/VST3/           (global, requiere sudo)
#
# Uso:
#   ./cpvst.sh                    # copia Release → carpeta usuario
#   ./cpvst.sh --debug            # copia build Debug
#   ./cpvst.sh --system           # copia a /Library/... (sudo)
#   ./cpvst.sh --dest /ruta/custom
#   ./cpvst.sh --build            # ./build.sh y luego copia
#   ./cpvst.sh --link             # symlink en lugar de copia (dev rápido)
#

set -euo pipefail

# Cubase no sigue bien un symlink: cada plugin se copia como bundle.
PLUGIN_NAMES=(
    DevicesForge.vst3
    VirtualDevices.vst3
)
WORKDIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$WORKDIR/build"
BUILD_TYPE="Release"
DEST="${HOME}/Library/Audio/Plug-Ins/VST3"
USE_SYSTEM=0
DO_BUILD=0
USE_LINK=0

usage() {
    cat <<'EOF'
cpvst.sh — instala DevicesForge.vst3 y VirtualDevices.vst3 para Cubase / hosts VST3

Opciones:
  --debug       Usar build/VST3/Debug/
  --release     Usar build/VST3/Release/ (default)
  --dest PATH   Carpeta destino (default: ~/Library/Audio/Plug-Ins/VST3)
  --system      Instalar en /Library/Audio/Plug-Ins/VST3 (sudo)
  --link        Crear symlink en lugar de copiar el bundle
  --build       Ejecutar ./build.sh --plugin-only antes de copiar
  -h, --help    Ayuda

Cubase 13: Studio → VST Plug-in Manager → (engranaje) → Rescan All
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug) BUILD_TYPE="Debug"; shift ;;
        --release) BUILD_TYPE="Release"; shift ;;
        --dest)
            DEST="$2"
            shift 2
            ;;
        --system)
            USE_SYSTEM=1
            DEST="/Library/Audio/Plug-Ins/VST3"
            shift
            ;;
        --link) USE_LINK=1; shift ;;
        --build) DO_BUILD=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *)
            echo "Opción desconocida: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ "$DO_BUILD" -eq 1 ]]; then
    BUILD_ARGS=(--plugin-only)
    [[ "$BUILD_TYPE" == "Debug" ]] && BUILD_ARGS=(--debug --plugin-only)
    echo "Compilando..."
    "$WORKDIR/build.sh" "${BUILD_ARGS[@]}"
fi

if [[ "$USE_SYSTEM" -eq 1 ]]; then
    sudo mkdir -p "$DEST"
else
    mkdir -p "$DEST"
fi

install_one() {
    local name="$1"
    local src="${BUILD_DIR}/VST3/${BUILD_TYPE}/${name}"
    local target="${DEST}/${name}"

    if [[ ! -d "$src" ]]; then
        echo "Error: no existe el plugin compilado:" >&2
        echo "  $src" >&2
        echo "" >&2
        echo "Compilá primero:  ./build.sh" >&2
        echo "  o:              ./cpvst.sh --build" >&2
        exit 1
    fi

    echo "  Origen:  $src"
    echo "  Destino: $target"
    if [[ "$USE_SYSTEM" -eq 1 ]]; then
        if [[ "$USE_LINK" -eq 1 ]]; then
            sudo ln -sfn "$src" "$target"
        else
            sudo rm -rf "$target"
            sudo ditto "$src" "$target"
        fi
    elif [[ "$USE_LINK" -eq 1 ]]; then
        rm -rf "$target"
        ln -sfn "$src" "$target"
    else
        rm -rf "$target"
        ditto "$src" "$target"
    fi
    echo ""
}

echo "============================================"
echo "  Instalar VST3 ($BUILD_TYPE)"
echo "============================================"

for name in "${PLUGIN_NAMES[@]}"; do
    install_one "$name"
done

echo "Listo."
echo ""
echo "Cubase LE / Elements / Pro:"
echo "  1. Cerrá Cubase si estaba abierto (recomendado tras instalar)."
echo "  2. Abrí Cubase → Studio → VST Plug-in Manager."
echo "  3. Engranaje → Rescan All."
echo "     El emulador figura como «Virtual Devices» (categoría Fx)."
echo "     El capturador sigue como «DevicesForge»."
echo ""
echo "Si agregaste una ruta custom en Cubase, usá:"
echo "  ./cpvst.sh --dest '/ruta/que/configuraste'"
